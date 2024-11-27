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

using field = std::string_view;
using message = std::vector<field>;

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
