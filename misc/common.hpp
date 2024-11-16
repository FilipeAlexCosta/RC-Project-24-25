#ifndef _COMMON_HPP_
#define _COMMON_HPP_

#include <sys/types.h>
#include <sys/socket.h>
#include <unordered_map>
#include <functional>
#include <string>

namespace net {
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
		void operator++();
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
	iterator begin(char delimiter = ' ') const;
	size_t end() const;
	const std::string& data() const;

private:
	std::string _raw;
};

struct action_map {
	using action = std::function<int(const message&)>;
	action_map& add_action(const std::string& name, const action& action);
	int execute(const std::string& command) const;
private:
	std::unordered_map<std::string, action> _actions;
};
};

#endif
