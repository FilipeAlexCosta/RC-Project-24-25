#ifndef _COMMON_HPP_
#define _COMMON_HPP_

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <unordered_map>
#include <functional>
#include <string>
#include <cstring>
#include <initializer_list>
#include "except.hpp"

#define DEFAULT_SEP ' '
#define DEFAULT_EOM '\n'
#define PLID_SIZE 6
#define MAX_PLAYTIME 600
#define MAX_PLAYTIME_SIZE 3
#define UDP_MSG_SIZE 128
#define MAX_RESEND 3
#define MAX_TRIALS '8'
#define GUESS_SIZE 4
#define DEFAULT_TIMEOUT 5
#define DEFAULT_LISTEN_CONNS 5
#define MAX_FSIZE 1024
#define MAX_FSIZE_LEN 4
#define MAX_FNAME_SIZE 24

namespace net {
static const std::string VALID_COLORS = "RGBYOP";

struct self_address {
	self_address(const std::string_view& other_addr, const std::string_view& other_port, int socktype, int family = AF_INET);
	self_address(const std::string_view& other_port, int socktype, int family = AF_INET);
	self_address(const self_address& other) = delete;
	self_address& operator=(const self_address& other) = delete;
	self_address(self_address&& other);
	self_address& operator=(self_address&& other);
	~self_address();
	bool valid() const;
	const addrinfo* unwrap() const;
	int family() const;
	int socket_type() const;
	bool is_passive() const;
private:
	addrinfo* _info{nullptr};
	int _fam{-1}, _sockt{-1};
	bool _passive;
};

struct other_address {
	socklen_t addrlen;
	sockaddr_in addr;
};

struct source {
	bool found_eom() const;
	bool finished() const;
	void reset();
protected:
	bool _found_eom = false;
	bool _finished = false;
};

struct file_source : public source {
	file_source(int fd);
	bool is_skippable(char c) const;
	void read_len(std::string& buf, size_t len, size_t& n, bool check_eom);
private:
	int _fd;
};

struct tcp_source : public file_source {
	tcp_source(int fd);
	bool is_skippable(char c) const;
};

struct string_source : public source {
	string_source(const std::string_view& source);
	string_source(std::string_view&& source);
	void read_len(std::string& buf, size_t len, size_t& n, bool check_eom);
	bool is_skippable(char c) const;
private:
	std::string_view _source;
	size_t _at = 0;
};

struct udp_source : public string_source {
	udp_source(const std::string_view& source);
	udp_source(std::string_view&& source);
	bool is_skippable(char c) const;
};

using field = std::string;
using message = std::vector<field>;

template<typename SOURCE>
struct stream {
	stream(const SOURCE& source, bool strict = true) : _source(source), _strict(strict) {}
	stream(SOURCE&& source, bool strict = true) : _source(std::move(source)), _strict(strict) {}

	message read(std::initializer_list<std::pair<size_t, size_t>> lens, bool check_eom = true) {
		message msg;
		for (auto len : lens)
			msg.push_back(read(len.first, len.second, check_eom));
		return msg;
	}

	std::string read(size_t min_len, size_t max_len, bool check_eom = true) {
		if (min_len > max_len || min_len == 0)
			return "";
		if (_source.finished())
			throw syntax_error{"Missing argument"};
		field buf;
		size_t bytes_read = 0;
		size_t off = 0;
		if (!_strict) {
			_source.read_len(buf, 1, bytes_read, true);
			if (bytes_read == 0)
				throw syntax_error{"Missing argument"};
			while (_source.is_skippable(buf[0])) {
				buf.clear();
				_source.read_len(buf, 1, bytes_read, true);
				if (bytes_read == 0)
					throw syntax_error{"Missing argument"};
			}
			off = 1;
		}
		_source.read_len(buf, min_len - off, bytes_read, check_eom);
		if (bytes_read < min_len - off)
			throw syntax_error{"Illegal argument"};
		for (size_t i = min_len; i < max_len; i++) {
			_source.read_len(buf, 1, bytes_read, true);
			if (bytes_read == 0)
				return buf;
			if (_source.is_skippable(buf[i])) {
				buf.pop_back();
				return buf;
			}
		}
		_source.read_len(buf, 1, bytes_read, true);
		if (bytes_read == 0)
			return buf;
		if (!_source.is_skippable(buf[max_len]))
			throw syntax_error{"Illegal argument"};
		buf.pop_back();
		return buf;
	}

	bool no_more_fields() {
		if (_source.found_eom())
			return true;
		field buf;
		while (true) {
			size_t bytes_read = 0;
			_source.read_len(buf, 1, bytes_read, true);
			if (bytes_read == 0)
				return true;
			if (_source.is_skippable(buf[0])) {
				buf.clear();
				continue;
			}
			return false;
		}
	}

	void exhaust() {
		while (!_source.finished()) {
			field buf;
			size_t bytes_read = 0;
			_source.read_len(buf, 1, bytes_read, true);
		}
		if (!_source.found_eom())
			throw formatting_error{"Missing EOM"};
	}

	bool found_eom() const {
		return _source.found_eom();
	}

	bool finished() const {
		return _source.finished();
	}

	void reset() {
		_source.reset();
	}

	void check_strict_end() const {
		if (_source.finished()) {
			if (_source.found_eom())
				return;
			throw missing_eom{};
		}
		throw formatting_error{"End wasn't 'strict'"};
	}
private:
	SOURCE _source;
	bool _strict;
};

struct out_stream {
	out_stream& write(const field& f);
	out_stream& write(char c);
	out_stream& write_and_fill(const field& f, size_t n, char fill);
	out_stream& prime();
	const std::string_view view() const;
private:
	std::string _buf;
	bool _primed = false;
};

struct udp_connection {
	udp_connection(self_address&& self, size_t timeout = DEFAULT_TIMEOUT);
	udp_connection(const udp_connection& other) = delete;
	udp_connection(udp_connection&& other);
	udp_connection& operator=(const udp_connection& other) = delete;
	udp_connection& operator=(udp_connection&& other);
	~udp_connection();
	bool valid() const;
	stream<udp_source> request(const out_stream& msg, other_address& other);
	void answer(const out_stream& msg, const other_address& other) const;
	stream<udp_source> listen(other_address& other);
	int get_fildes(); // careful with this
private:
	self_address _self;
	int _fd{-1};
	char _buf[UDP_MSG_SIZE];
};

struct tcp_connection {
	tcp_connection();
	tcp_connection(int fd, size_t timeout = DEFAULT_TIMEOUT);
	tcp_connection(const self_address& self, size_t timeout = DEFAULT_TIMEOUT);
	tcp_connection(const tcp_connection& other) = delete;
	tcp_connection(tcp_connection&& other);
	tcp_connection& operator=(const tcp_connection& other) = delete;
	tcp_connection& operator=(tcp_connection&& other);
	~tcp_connection();
	bool valid() const;
	stream<tcp_source> request(const out_stream& msg) const;
	net::stream<net::tcp_source> to_stream() const;
	void answer(const out_stream& msg) const;
protected:
	int _fd{-1};
};

struct tcp_server : protected tcp_connection {
	tcp_server(const self_address& self, size_t sub_conns = DEFAULT_LISTEN_CONNS);
	tcp_connection accept_client(other_address& other);
	bool valid() const;
	int get_fildes(); // careful with this
};

bool is_valid_plid(const field& field);

bool is_valid_max_playtime(const field& field);

bool is_valid_color(const field& field);

bool is_valid_fname(const field& field);

bool is_valid_fsize(size_t fsize);

template<typename SOURCE, typename... ARGS>
struct action_map {
	using arg_stream = stream<SOURCE>;
	using action = std::function<void(arg_stream&, ARGS...)>;

	void add_action(const std::string_view& name, const action& action) {
		_actions.insert({std::move(static_cast<std::string>(name)), action});
	}

	void add_action(std::initializer_list<const std::string_view> names, const action& action) {
		for (auto name : names)
			add_action(name, action);
	}

	void execute(arg_stream& strm, ARGS&&... args) const {
		std::string comm = strm.read(1, SIZE_MAX);
		auto it = _actions.find(comm);
		if (it == _actions.end())
			throw syntax_error{"Unknown action"};
		return it->second(strm, std::forward<ARGS>(args)...);
	}
private:
	std::unordered_map<std::string, action> _actions;
};
};

#endif
