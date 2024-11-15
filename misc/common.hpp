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
		field(const message& parent, int8_t direction, ssize_t index = 0)
			: _parent{parent}, _direction{direction}, _idx{index} {
			_idx += _parent._from;
		}

		void operator++(int) { _idx += _direction; }
		void operator++() { _idx += _direction; }
		char operator*() const { return _parent._raw[_idx]; }
		bool operator!=(const field& other) const { return _idx != other._idx; }
		size_t length() const { return _parent._to - _parent._from; }

	private:
		ssize_t _idx;
		int8_t _direction;
		const message& _parent;
	};

	message(const std::string& msg) : _raw{msg} {
		if (_raw.length() == 0) {
			_to = 1;
			return;
		}
		if (_raw[_from] == DEL) {
			for (; _to < _raw.length() && _raw[_to] == DEL; _to++);
		} else {
			for (; _to < _raw.length() && _raw[_to] != DEL; _to++);
		}
	}

	void next_field() {
		if (_raw[_from] == DEL) {
			_from = _to;
			_to++;
			for (; _to < _raw.length() && _raw[_to] != DEL; _to++);
		} else {
			_from = _to;
			_to++;
			for (; _to < _raw.length() && _raw[_to] == DEL; _to++);
		}
	}

	bool has_next() const { return _to <= _raw.length(); }
	field begin() const { return field(*this, 1); }
	field end() const { return field(*this, 1, _to - _from); }
	field rbegin() const { return field(*this, -1, _to - _from - 1); }
	field rend() const { return field(*this, -1, -1); }

private:
	size_t _from = 0;
	size_t _to = 0;
	std::string _raw;
	static const char DEL = ' ';
};

struct action_map {
private:
	std::unordered_map<std::string, std::function<void(message&)>> _actions;
};
};

#endif
