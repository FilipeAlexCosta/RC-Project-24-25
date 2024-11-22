#ifndef _COMMON_HPP_
#define _COMMON_HPP_

#include <sys/types.h>
#include <sys/socket.h>
#include <unordered_map>
#include <functional>
#include <string>
#include <initializer_list>

#define DEFAULT_SEP ' '
#define DEFAULT_EOM '\n'
#define PLID_SIZE 6
#define MAX_PLAYTIME_SIZE 3
#define UDP_MSG_SIZE 128

namespace net {
enum class action_status {
	OK,
	ERR,
	UNK_ACTION,
	MISSING_ARG,
	EXCESS_ARGS,
	BAD_ARG,
	ONGOING_GAME,
	NOT_IN_GAME
};

using field = std::string_view;
using message = std::vector<field>;

std::string status_to_message(action_status status);

template<typename... Ts>
ssize_t udp_write(int socket_fd, int flags, const struct sockaddr* dest_addr, socklen_t addrlen, Ts... args) {
	static_assert(sizeof...(Ts) > 0, "udp_write: must write at least one argument");
	constexpr size_t buf_size = (sizeof(args) + ...);
	static_assert(buf_size > 0, "udp_write: must write at least one byte");
	char buffer[buf_size];
	size_t start = 0;
	(([&buffer, &start, &args]() {
		memcpy(buffer + start, (char*) &args, sizeof(args));
		start += sizeof(args);
	}()), ...);
	return sendto(socket_fd, buffer, buf_size, flags, dest_addr, addrlen);
}

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
	using action = std::function<action_status(const std::string&)>;
	void add_action(const std::string_view& name, const action& action);
	void add_action(std::initializer_list<const std::string_view> names, const action& action);
	action_status execute(const std::string& command) const;
private:
	std::unordered_map<std::string, action> _actions;
};
};

#endif
