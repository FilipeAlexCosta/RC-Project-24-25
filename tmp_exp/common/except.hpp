#ifndef _EXCEPTIONS_H_
#define _EXCEPTIONS_H_

#include <stdexcept>

namespace net {
/// Represents a system call fail or similar.
struct system_error : public std::runtime_error {
	system_error(const std::string& what);
};

/// Represents an unexpected error from the program itself.
/// For instance a game file with a wrong format.
struct corruption_error : public std::runtime_error {
	corruption_error(const std::string& what);
};

/// Represents when the peer answered in an illegal way.
struct bad_response : public std::runtime_error {
	bad_response(const std::string& what);
};

/// Represents game errors such as no ongoing game, etc...
struct game_error : public std::runtime_error {
	game_error(const std::string& what);
};

/// Represents an error in an interaction like client-server communication, etc...
struct interaction_error : public std::runtime_error {
	interaction_error(const std::string& what);
};

/// Represents a syntax error like a bad plid.
struct syntax_error : public interaction_error {
	syntax_error(const std::string& what);
};

/// Represents a format error like missing an eom or too many separators.
struct formatting_error : public interaction_error {
	formatting_error(const std::string& what);
};

/// Represents when a message was not terminated with an EOM.
struct missing_eom : public formatting_error {
	missing_eom();
};

/// Represents an IO error like a file incorrectly closing/opening.
struct io_error : public std::runtime_error {
	io_error(const std::string& what);
};

/// Represents a generic socket error.
struct socket_error : public std::runtime_error {
	socket_error(const std::string& what);
};

/// Represents when a sendto/recvfrom and similar functions failed.
struct conn_error : public socket_error {
	conn_error(const std::string& what);
};

/// Represents when the peer closed their socket midway through writing
/// for instance.
struct socket_closed_error : public socket_error {
	socket_closed_error(const std::string& what);
};
};

#endif
