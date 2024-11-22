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
#include <initializer_list>

#define DEFAULT_SEP ' '
#define DEFAULT_EOM '\n'
#define PLID_SIZE 6
#define MAX_PLAYTIME_SIZE 3
#define UDP_MSG_SIZE 128
#define MAX_RESEND 3

namespace net {
typedef struct socket_context {
	int socket_fd;
	const struct addrinfo* receiver_info;
	struct sockaddr_in* sender_addr;
	socklen_t* sender_addr_len;
} socket_context;

enum class action_status {
	OK,
	ERR,
	UNK_ACTION,
	MISSING_ARG,
	EXCESS_ARGS,
	BAD_ARG,
	ONGOING_GAME,
	NOT_IN_GAME,
	SEND_ERR,
	CONN_TIMEOUT,
	RECV_ERR,
};

using field = std::string_view;
using message = std::vector<field>;

std::string status_to_message(action_status status);

action_status udp_request(const char* req, uint32_t req_sz, net::socket_context& udp_info, char* ans, uint32_t ans_sz, int& read);

std::pair<action_status, message> get_fields(
	const char* buf,
	size_t buf_sz,
	std::initializer_list<int> field_szs
);

std::pair<action_status, message> get_fields_strict(
	const char* buf,
	size_t buf_sz,
	std::initializer_list<uint32_t> field_szs,
	char sep = DEFAULT_SEP
);

int prepare_buffer(char* buf, int buf_sz, message msg, char sep = DEFAULT_SEP, char eom = DEFAULT_EOM);

action_status is_valid_plid(const field& field);

action_status is_valid_max_playtime(const field& field);

action_status is_valid_color(const field& field);

void fill_max_playtime(char res[MAX_PLAYTIME_SIZE], const field& max_playtime);

struct action_map {
	using action = std::function<action_status(const std::string&, socket_context& socket_info)>;
	void add_action(const std::string_view& name, const action& action);
	void add_action(std::initializer_list<const std::string_view> names, const action& action);
	action_status execute(const std::string& command, socket_context& socket_info) const;
private:
	std::unordered_map<std::string, action> _actions;
};
};

#endif
