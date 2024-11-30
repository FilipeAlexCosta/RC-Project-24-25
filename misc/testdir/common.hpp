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

#define DEFAULT_SEP ' '
#define DEFAULT_EOM '\n'
#define PLID_SIZE 6
#define MAX_PLAYTIME_SIZE 3
#define UDP_MSG_SIZE 128
#define MAX_RESEND 3
#define MAX_TRIALS '8'
#define GUESS_SIZE 4

namespace net {
struct socket_context {
	int socket_fd = -1;
	addrinfo* receiver_info = nullptr;
	sockaddr_in sender_addr;
	socklen_t sender_addr_len = sizeof(sender_addr);

	socket_context(const std::string_view& rec_addr, const std::string_view& rec_port, int type);
	~socket_context();

	int set_timeout(size_t s);
	bool is_valid();
};

enum class action_status {
	OK,
	ERR,
	RET_ERR,
	UNK_ACTION,
	MISSING_ARG,
	EXCESS_ARGS,
	BAD_ARG,
	ONGOING_GAME,
	NOT_IN_GAME,
	SEND_ERR,
	CONN_TIMEOUT,
	RECV_ERR,
	MISSING_EOM,
	UNK_REPLY,
	UNK_STATUS,
	START_NOK,
	START_ERR,
	DEBUG_ERR,
	QUIT_EXIT_ERR,
	TRY_NT,
	TRY_ERR,
	TRY_DUP,
	TRY_INV,
	TRY_NOK,
	TRY_ENT,
	TRY_ETM,

};

struct source {
	bool found_eom() const;
	bool finished() const;
protected:
	bool _found_eom = false;
	bool _finished = false;
};

struct file_source : public source {
	file_source(int fd);
	bool is_skippable(char c) const;
	action_status read_len(std::string& buf, size_t len, size_t& n, bool check_eom);
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
	action_status read_len(std::string& buf, size_t len, size_t& n, bool check_eom);
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

	std::pair<action_status, message> read(std::initializer_list<std::pair<size_t, size_t>> lens, bool check_eom = true) {
		message msg;
		for (auto len : lens) {
			auto [res, fld] = read(len.first, len.second, check_eom);
			msg.push_back(fld);
			if (res != action_status::OK)
				return {res, msg};
		}
		return {action_status::OK, msg};
	}

	std::pair<action_status, std::string> read(size_t min_len, size_t max_len, bool check_eom = true) {
		if (min_len > max_len || min_len == 0)
			return {action_status::OK, {}};
		if (_source.found_eom())
			return {action_status::MISSING_ARG, {}};
		field buf;
		size_t bytes_read = 0;
		size_t off = 0;
		if (!_strict) {
			auto res = _source.read_len(buf, 1, bytes_read, check_eom);
			if (res != action_status::OK)
				return {res, buf};
			if (bytes_read == 0)
				return {net::action_status::MISSING_ARG, {}};
			while (_source.is_skippable(buf[0])) {
				buf.clear();
				res = _source.read_len(buf, 1, bytes_read, check_eom);
				if (res != action_status::OK)
					return {res, buf};
				if (bytes_read == 0)
					return {net::action_status::MISSING_ARG, {}};
			}
			off = 1;
		}
		auto res = _source.read_len(buf, min_len - off, bytes_read, check_eom);
		if (res != action_status::OK)
			return {res, buf};
		if (bytes_read < min_len - off)
			return {net::action_status::BAD_ARG, buf};
		for (size_t i = min_len; i < max_len; i++) {
			res = _source.read_len(buf, 1, bytes_read, check_eom);
			if (res != action_status::OK || bytes_read == 0)
				return {res, buf};
			if (_source.is_skippable(buf[i])) {
				buf.pop_back();
				return {action_status::OK, buf};
			}
		}
		res = _source.read_len(buf, 1, bytes_read, check_eom);
		if (bytes_read == 0)
			return {net::action_status::OK, buf};
		if (!_source.is_skippable(buf[max_len]))
			return {action_status::BAD_ARG, buf};
		buf.pop_back();
		return {action_status::OK, buf};
	}

	action_status no_more_fields() {
		if (_source.found_eom())
			return net::action_status::OK;
		field buf;
		while (true) {
			size_t bytes_read = 0;
			auto res = _source.read_len(buf, 1, bytes_read, true);
			if (res != net::action_status::OK)
				return res;
			if (bytes_read == 0)
				return net::action_status::OK;
			if (_source.is_skippable(buf[0])) {
				buf.clear();
				continue;
			}
			return net::action_status::EXCESS_ARGS;
		}
	}

	action_status exhaust() {
		while (!_source.finished()) {
			field buf;
			size_t bytes_read = 0;
			_source.read_len(buf, 1, bytes_read, true);
		}
		if (!_source.found_eom())
			return net::action_status::MISSING_EOM;
		return net::action_status::OK;
	}

	bool found_eom() const {
		return _source.found_eom();
	}

	bool finished() const {
		return _source.finished();
	}

	action_status check_strict_end() const {
		if (_source.finished()) {
			if (_source.found_eom())
				return net::action_status::OK;
			return net::action_status::MISSING_EOM;
		}
		return net::action_status::BAD_ARG; // TODO: maybe SEP_ERR instead
	}
private:
	SOURCE _source;
	bool _strict;
};

struct out_stream {
	out_stream& write(const field& f);
	out_stream& write_and_fill(const field& f, size_t n, char fill);
	out_stream& prime();
	const std::string_view view() const;
private:
	std::string _buf;
	bool _primed = false;
};

std::string status_to_message(action_status status);

action_status udp_request(const out_stream& out_str, net::socket_context& udp_info, char* ans, uint32_t ans_sz, int& read);

action_status tcp_request(const char* req, uint32_t req_sz, net::socket_context& tcp_info, char* ans, uint32_t ans_sz, int& r);

action_status is_valid_plid(const field& field);

action_status is_valid_max_playtime(const field& field);

action_status is_valid_color(const field& field);

void fill_max_playtime(char res[MAX_PLAYTIME_SIZE], const field& max_playtime);

template<typename SOURCE, typename... ARGS>
struct action_map {
	using arg_stream = stream<SOURCE>;
	using action = std::function<action_status(arg_stream&, ARGS...)>;

	void add_action(const std::string_view& name, const action& action) {
		_actions.insert({std::move(static_cast<std::string>(name)), action});
	}

	void add_action(std::initializer_list<const std::string_view> names, const action& action) {
		for (auto name : names)
			add_action(name, action);
	}

	action_status execute(arg_stream& strm, ARGS... args) const {
		auto [status, comm] = strm.read(1, SIZE_MAX);
		if (status != action_status::OK)
			return status;
		auto it = _actions.find(comm);
		if (it == _actions.end())
			return action_status::UNK_ACTION;
		return it->second(strm, args...);
	}
private:
	std::unordered_map<std::string, action> _actions;
};
};

#endif
