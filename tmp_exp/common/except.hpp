#ifndef _EXCEPTIONS_H_
#define _EXCEPTIONS_H_

#include <stdexcept>

namespace net {
struct system_error : public std::runtime_error {
	system_error(const std::string& what) : std::runtime_error{what} {}
};

struct corruption_error : public std::runtime_error {
	corruption_error(const std::string& what) : std::runtime_error{what} {}
};

struct bad_response : public std::runtime_error {
	bad_response(const std::string& what) : std::runtime_error{what} {}
};

struct game_error : public std::runtime_error {
	game_error(const std::string& what) : std::runtime_error{what} {}
};

struct interaction_error : public std::runtime_error {
	interaction_error(const std::string& what) : std::runtime_error{what} {}
};

struct syntax_error : public interaction_error {
	syntax_error(const std::string& what) : interaction_error{what} {}
};

struct formatting_error : public interaction_error {
	formatting_error(const std::string& what) : interaction_error{what} {}
};

struct missing_eom : public formatting_error {
	missing_eom() : formatting_error{"Missing EOM"} {}
};

struct io_error : public std::runtime_error {
	io_error(const std::string& what) : std::runtime_error{what} {}
};

struct socket_error : public std::runtime_error {
	socket_error(const std::string& what) : std::runtime_error{what} {}
};

struct conn_error : public socket_error {
	conn_error(const std::string& what) : socket_error{what} {}
};

struct socket_closed_error : public socket_error {
	socket_closed_error(const std::string& what) : socket_error{what} {}
};
};

#endif
