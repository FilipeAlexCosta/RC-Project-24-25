#ifndef _COMMON_HPP_
#define _COMMON_HPP_

#include <sys/types.h>
#include <sys/socket.h>
#include <unordered_map>
#include <functional>
#include <string>
#include <initializer_list>

#define DEFAULT_MSG_DELIMITER ' '
#define DEFAULT_END_OF_MSG '\n'
#define GUESS_SIZE 4

namespace net {
enum class action_status {
	OK,
	UNK_ACTION,
	MISSING_ARG,
	EXCESS_ARGS,
	BAD_ARG,
	ONGOING_GAME,
	NOT_IN_GAME
};

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

struct message {
	struct iterator {
		using field = std::string_view;

		iterator(const message& message, char delimiter);
		iterator& operator++();
		bool operator!=(size_t other) const;
		bool operator==(size_t other) const;
		field operator*() const;
		bool is_in_delimiter_phase() const;

	private:
		const message& _parent;
		size_t _from = 0, _to = 0;
		char _delimiter;
		bool _in_del_phase;
	};

	message(const std::string& msg);
	iterator begin(char delimiter = DEFAULT_MSG_DELIMITER) const;
	size_t end() const;
	const std::string& data() const;

private:
	std::string _raw;
};

struct action_map {
	using action = std::function<action_status(const message&)>;
	void add_action(const std::string_view& name, const action& action);
	void add_action(std::initializer_list<const std::string_view> names, const action& action);
	action_status execute(const std::string& command) const;
private:
	std::unordered_map<std::string, action> _actions;
};
};

#endif
