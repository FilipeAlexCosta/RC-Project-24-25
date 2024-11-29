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

struct tcp_source {
	tcp_source(int fd) : _socket_fd{fd} {}

	bool is_skippable(char c) const {
		return c == DEFAULT_SEP;
	}

	action_status read_len(std::string& buf, size_t len, size_t& n, bool check_eom) {
		n = 0;
		if (len == 0 || _found_eom)
			return action_status::OK;
		char temp[len];
		while (len != 0) {
			int res = read(_socket_fd, temp, len);
			if (res < 0)
				return action_status::CONN_TIMEOUT;
			if (res == 0) {
				_found_eom = true;
				return net::action_status::OK;
			}
			len -= res;
			if (check_eom && temp[res - 1] == DEFAULT_EOM) {
				res--;
				len = 0;
				_found_eom = true;
			}
			buf.append(temp, temp + res);
			n += res;
		}
		return action_status::OK;
	}

	bool found_eom() const {
		return _found_eom;
	}
private:
	int _socket_fd;
	bool _found_eom = false;
};

struct string_source {
	string_source(const std::string_view& source) : _source(source) {}
	string_source(std::string_view&& source) : _source(std::move(source)) {}

	action_status read_len(std::string& buf, size_t len, size_t& n, bool check_eom) {
		if (len == 0 || _found_eom) {
			n = 0;
			return action_status::OK;
		}
		size_t end = _at + len;
		if (end > _source.size()) {
			end = _source.size();
			_found_eom = true;
		}
		buf.append(std::begin(_source) + _at, std::begin(_source) + end);
		n = end - _at;
		_at = end;
		if (check_eom && buf.back() == DEFAULT_EOM) {
			n--;
			buf.pop_back();
			_found_eom = true;
		}
		return net::action_status::OK;
	}

	bool is_skippable(char c) const {
		return std::isspace(c);
	}

	bool found_eom() const {
		return _found_eom;
	}
private:
	std::string_view _source;
	size_t _at = 0;
	bool _found_eom = false;
};

struct udp_source : public string_source {
	udp_source(const std::string_view& source) : string_source(source) {}
	udp_source(std::string_view&& source) : string_source(std::move(source)) {}

	bool is_skippable(char c) const {
		return c != DEFAULT_SEP;
	}
};

using field = std::string_view;
using message = std::vector<field>;

template<typename SOURCE>
struct stream {
	stream(const SOURCE& source, bool strict = true) : _source(source), _strict(strict) {}
	stream(SOURCE&& source, bool strict = true) : _source(std::move(source)), _strict(strict) {}

	std::pair<action_status, std::vector<std::string>> read(std::initializer_list<std::pair<size_t, size_t>> lens) {
		std::vector<std::string> msg;
		for (auto len : lens) {
			auto [res, fld] = read(len.first, len.second);
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
		std::string buf;
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

	action_status no_more_fields(bool check_eom = true) {
		if (_source.found_eom())
			return net::action_status::OK;
		std::string buf;
		while (true) {
			size_t bytes_read = 0;
			auto res = _source.read_len(buf, 1, bytes_read, check_eom);
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

	bool found_eom() const {
		return _source.found_eom();
	}
private:
	SOURCE _source;
	bool _strict;
};

std::string status_to_message(action_status status);

action_status udp_request(const char* req, uint32_t req_sz, net::socket_context& udp_info, char* ans, uint32_t ans_sz, int& read);

action_status tcp_request(const char* req, uint32_t req_sz, net::socket_context& tcp_info, char* ans, uint32_t ans_sz, int& r);

std::pair<action_status, message> get_fields(
	const char* buf,
	size_t buf_sz,
	std::initializer_list<int> field_szs
);

std::pair<action_status, message> get_fields_strict(
	const char* buf,
	size_t buf_sz,
	std::initializer_list<int> field_szs,
	char sep = DEFAULT_SEP
);

int prepare_buffer(char* buf, int buf_sz, message msg, char sep = DEFAULT_SEP, char eom = DEFAULT_EOM);

action_status is_valid_plid(const field& field);

action_status is_valid_max_playtime(const field& field);

action_status is_valid_color(const field& field);

void fill_max_playtime(char res[MAX_PLAYTIME_SIZE], const field& max_playtime);

template<typename... Args>
struct action_map {
	using action = std::function<action_status(const std::string&, Args...)>;

	void add_action(const std::string_view& name, const action& action) {
		_actions.insert({std::move(static_cast<std::string>(name)), action});
	}

	void add_action(std::initializer_list<const std::string_view> names, const action& action) {
		for (auto name : names)
			add_action(name, action);
	}

	action_status execute(const std::string& command, Args... args) const {
		size_t from  = 0;
		for (; from < command.size() && std::isspace(command[from]); from++);
		if (from > command.size())
			return action_status::UNK_ACTION;
		size_t to = from + 1;
		for (; to < command.size() && (!std::isspace(command[to])); to++);
		auto it = _actions.find(std::string(std::begin(command) + from, std::begin(command) + to));
		if (it == _actions.end())
			return action_status::UNK_ACTION;
		return it->second(command, args...);
	}
private:
	std::unordered_map<std::string, action> _actions;
};
};

#endif
