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
	struct field {
		field(const message& parent, ssize_t index = 0);
		void operator++(int);
		void operator++();
		char operator[](int) const;
		char operator*() const;
		bool operator!=(const field& other) const;
		size_t length() const;

	protected:
		ssize_t _idx;
		const message& _parent;
	};

	struct reverse_field : public field {
		reverse_field(const message& parent, ssize_t index = 0);
		void operator++(int);
		void operator++();
		char operator[](int) const;
	};

	message(const std::string& msg);
	void next_field();
	bool is_in_delimiter_phase() const;
	bool has_next() const;
	field begin() const;
	field end() const;
	reverse_field rbegin() const;
	reverse_field rend() const;

private:
	size_t _from = 0;
	size_t _to = 0;
	bool _in_del_phase;
	std::string _raw;
	static const char DEL = ' ';
};

struct action_map {
private:
	std::unordered_map<std::string, std::function<void(message&)>> _actions;
};
};

#endif
