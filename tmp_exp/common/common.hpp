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
#include "except.hpp"

#define DEFAULT_SEP ' '
#define DEFAULT_EOM '\n'
#define PLID_SIZE 6
#define MAX_PLAYTIME 600
#define MAX_PLAYTIME_SIZE 3
#define UDP_MSG_SIZE 128
#define MAX_RESEND 3
#define MAX_TRIALS '8'
#define GUESS_SIZE 4
#define DEFAULT_TIMEOUT 5
#define DEFAULT_LISTEN_CONNS 5
#define MAX_FSIZE 1024
#define MAX_FSIZE_LEN 4
#define MAX_FNAME_SIZE 24

namespace net {
static const std::string VALID_COLORS = "RGBYOP";

/// Encapsulates the address obtained using getaddrinfo
struct self_address {
	/// Gets an address local to other_addr
	self_address(const std::string_view& other_addr, const std::string_view& other_port, int socktype, int family = AF_INET);

	/// Gets a local address (passes NULL into getaddrinfo)
	/// Also sets it to passive
	self_address(const std::string_view& other_port, int socktype, int family = AF_INET);

	self_address(const self_address& other) = delete;

	self_address& operator=(const self_address& other) = delete;

	self_address(self_address&& other);

	self_address& operator=(self_address&& other);

	~self_address();

	/// Checks if the address is valid
	bool valid() const;

	/// Unwraps the address into the underlying address structure
	const addrinfo* unwrap() const;

	/// Gets the family of the address (initially passed
	/// as the family argument in the constructor)
	int family() const;

	/// Gets the socket type of the address (initially passed
	/// as the socktype argument in the constructor)
	int socket_type() const;

	/// Checks whether or not the address is passive
	bool is_passive() const;
private:
	addrinfo* _info{nullptr};
	int _fam{-1}, _sockt{-1};
	bool _passive;
};

/// Encapsulates the address of the peer that sent the message to "you"
struct other_address {
	socklen_t addrlen;
	sockaddr_in addr;
};

/// Represents a generic input source that can be used as a template argument
/// to a net::stream<T>.
/// Sources do not own the underlying structures (i.e. a file_source will
/// not close the file descriptor it references when the destructor is called)
/// It is NOT recommended to use sources directly. Use them inside a net::stream<T>.
struct source {
	/// Returns whether or not the end of message was found.
	/// If found_eom() returns true => finished() true.
	bool found_eom() const;

	/// Returns whether or not more data is expected.
	/// finished() evaluating to true does not imply EOM was found.
	/// For instance, if a file_source returned 0 on read (EOF was found),
	/// but no EOM was read, then finished() shall evaluate to true, but
	/// found_eom() will evaluate to false.
	bool finished() const;

	/// Resets the state of the stream ("forgets" if the EOM and/or
	/// the finish was found)
	void reset();
protected:
	bool _found_eom = false;
	bool _finished = false;
};

/// Reads from a file.
struct file_source : public source {
	/// This does NOT own the file descriptor.
	/// You will have to close it yourself!
	file_source(int fd);

	/// Returns true if c is whitespace; false otherwise.
	bool is_skippable(char c) const;

	/// Reads len bytes if possible into buf.
	/// After returning, n will be set to the precise number of bytes read.
	/// If check_eom is true, then everytime the underlying read call
	/// returns it checks if it ends on EOM, returning early if it does.
	///
	/// Throws missing_eom if the EOF is reached before the EOM.
	void read_len(std::string& buf, size_t len, size_t& n, bool check_eom);
private:
	int _fd;
};

/// Overloads is_skippable as to implement the semantics of reading
/// separators from a tcp socket.
struct tcp_source : public file_source {
	/// See file_source::file_source
	tcp_source(int fd);

	/// Returns true if c is the DEFAULT_SEP; false otherwise.
	bool is_skippable(char c) const;
};

/// Represents a string source (it reads from a string as if it were
/// a stream). It does NOT own the underlying string: destroying the
/// source string mid-use is undefined behaviour.
struct string_source : public source {
	string_source(const std::string_view& source);
	string_source(std::string_view&& source);

	/// See file_source::read_len.
	/// If check_eom is true then it will check whether or not there
	/// is a EOM either at the end of the parsed field or at the
	/// end of the underlying string (in the case the len overshoots
	/// the size).
	/// 
	/// Throws missing_eom if the end of the string is reached before
	/// the EOM is found.
	void read_len(std::string& buf, size_t len, size_t& n, bool check_eom);

	/// Returns true if c is whitespace; false otherwise.
	bool is_skippable(char c) const;
private:
	std::string_view _source;
	size_t _at = 0;
};

/// Overloads is_skippable as to implement the semantics of reading
/// from a udp socket.
struct udp_source : public string_source {
	udp_source(const std::string_view& source);
	udp_source(std::string_view&& source);

	/// Returns true if c is the DEFAULT_SEP; false otherwise.
	bool is_skippable(char c) const;
};

using field = std::string;
using message = std::vector<field>;

/// Encapsulates a source in order to implement read semantics.
/// It is input only: does not implement write functions.
/// SOURCE should derive source and implement a read_len function
/// as shown in file_source and string_source.
/// It should also implement an is_skippable function, depending
/// on which characters are considered to be separators.
/// If the stream is set to strict, then exactly one separator is allowed
/// in between fields. Otherwise, the stream automatically reads separators
/// until character that evaluates is_skippable() to false is found, or
/// the EOM/finish is reached.
template<typename SOURCE>
struct stream {
	stream(const SOURCE& source, bool strict = true) : _source(source), _strict(strict) {}
	stream(SOURCE&& source, bool strict = true) : _source(std::move(source)), _strict(strict) {}

	/// Bulk operator of stream::read().
	/// For instance, read({{1, 2}, {1, 1}}) would first read a field
	/// of size 1 or 2 and then a field of length 1 into the returned
	/// message.
	message read(std::initializer_list<std::pair<size_t, size_t>> lens, bool check_eom = true) {
		message msg;
		for (auto len : lens)
			msg.push_back(read(len.first, len.second, check_eom));
		return msg;
	}

	/// Reads a field with a minimum size of min_len and a maximum size
	/// of max_len.
	/// If check_eom is enabled, then the field can be cut short in the
	/// case EOM is found before reading max_len bytes. In the case it
	/// is not set, then it will retry reading from the source until
	/// the source throws an error or the remaining bytes arrive.
	///
	/// Recommendation: disable check_eom only when you want to read
	/// an exact number of bytes that may contain the DEFAULT_EOM
	/// among them.
	///
	/// Throws:
	/// 1. syntax_error if not argument was read or if it was illegal
	/// (did not match the min_len and max_len requirements).
	/// 2. missing_eom if the source finished but the eom was not found.
	/// 3. io_error/socket_error depending on the underlying source.
	field read(size_t min_len, size_t max_len, bool check_eom = true) {
		if (min_len > max_len || min_len == 0)
			return "";
		if (_source.finished())
			throw syntax_error{"Missing argument"};
		field buf;
		size_t bytes_read = 0;
		size_t off = 0;
		if (!_strict) { // if not strict, skip skippable characters
			_source.read_len(buf, 1, bytes_read, true);
			if (bytes_read == 0) // if ended early => fail
				throw syntax_error{"Missing argument"};
			while (_source.is_skippable(buf[0])) {
				buf.clear();
				_source.read_len(buf, 1, bytes_read, true);
				if (bytes_read == 0)
					throw syntax_error{"Missing argument"};
			}
			off = 1; // if this is reached then at least one
			// non-skippable char has been read => only min_len - 1 to go
		}
		_source.read_len(buf, min_len - off, bytes_read, check_eom);
		if (bytes_read < min_len - off) // did not even read minimum amount
			throw syntax_error{"Illegal argument"};
		for (size_t i = min_len; i < max_len; i++) { // read until max_len or
			_source.read_len(buf, 1, bytes_read, true); // a skippable char is found
			if (bytes_read == 0)
				return buf;
			if (_source.is_skippable(buf[i])) {
				buf.pop_back(); // remove skippable char
				return buf;
			}
		}

		// checks if a field is separated like "abc def" (it eats the ' ')
		_source.read_len(buf, 1, bytes_read, true);
		if (bytes_read == 0) // did not read anything
			return buf;
		if (!_source.is_skippable(buf[max_len])) // did not read a separator
			throw syntax_error{"Illegal argument"};
		buf.pop_back(); // ignore the separator
		return buf;
	}

	/// Returns true if no more skippable chars are read before
	/// hitting the EOM. False otherwise.
	/// Throws missing_eom in case it the source closes before
	/// EOM is found.
	/// WARNING: the first non-skippable char found is destroyed.
	bool no_more_fields() {
		if (_source.found_eom())
			return true;
		field buf;
		while (true) {
			size_t bytes_read = 0;
			_source.read_len(buf, 1, bytes_read, true);
			if (bytes_read == 0)
				return true;
			if (_source.is_skippable(buf[0])) {
				buf.clear();
				continue;
			}
			return false;
		}
	}

	/// Reads until the source finishes/EOM is found.
	/// If the source finishes before EOM is found it throws
	/// missing_em.
	void exhaust() {
		while (!_source.finished()) {
			field buf;
			size_t bytes_read = 0;
			_source.read_len(buf, 1, bytes_read, true);
		}
		if (!_source.found_eom())
			throw missing_eom{};
	}

	/// Returns true if eom has been found; false otherwise.
	bool found_eom() const {
		return _source.found_eom();
	}

	/// Returns true if the source has finished receiving; false otherwise.
	bool finished() const {
		return _source.finished();
	}

	/// Resets the state of the stream.
	void reset() {
		_source.reset();
	}

	/// Source found EOM => simply returns;
	/// Source finished but did not find EOM => throws missing_eom.
	/// Otherwise => throws formatting_error.
	void check_strict_end() const {
		if (_source.finished()) {
			if (_source.found_eom())
				return;
			throw missing_eom{};
		}
		throw formatting_error{"End wasn't 'strict'"};
	}
private:
	SOURCE _source;
	bool _strict;
};

/// Buffers the writes into an underlying buffer.
/// Automatically separates each write with a DEFAULT_SEP.
struct out_stream {
	out_stream& write(const field& f);
	out_stream& write(char c);

	/// If the length of f is smaller than n, fills it with the 'fill'
	/// on the left until a length of n is reached.
	out_stream& write_and_fill(const field& f, size_t n, char fill);

	/// Prepares the message to be sent (adds a DEFAULT_EOM at the end).
	out_stream& prime();

	/// ALlows viewing the underlying data.
	const std::string_view view() const;
private:
	std::string _buf;
};

/// Encapsulates a udp socket.
struct udp_connection {
	udp_connection(self_address&& self, size_t timeout = DEFAULT_TIMEOUT);

	udp_connection(const udp_connection& other) = delete;

	udp_connection(udp_connection&& other);

	udp_connection& operator=(const udp_connection& other) = delete;

	udp_connection& operator=(udp_connection&& other);

	~udp_connection();

	/// Returns true if the socket is ready to use; false otherwise.
	bool valid() const;

	/// Sends 'msg' through UDP and waits for a response.
	/// On return, other is set to the address of the peer that answered.
	/// Returns a udp stream with the received message.
	/// Implements timeout.
	/// Limited to a maximum size of UDP_MSG_SIZE byte datagrans.
	stream<udp_source> request(const out_stream& msg, other_address& other);

	/// Sends 'msg' to other.
	void answer(const out_stream& msg, const other_address& other) const;

	/// Waits for a message (only use if the socket is passive).
	stream<udp_source> listen(other_address& other);

	/// Returns the underlying file descriptor.
	/// CLosing the returned file descriptor is undefined behaviour.
	int get_fildes();
private:
	self_address _self;
	int _fd{-1};
	char _buf[UDP_MSG_SIZE];
};

/// Represents a non-passive tcp socket.
struct tcp_connection {
	tcp_connection();

	tcp_connection(int fd, size_t timeout = DEFAULT_TIMEOUT);

	tcp_connection(const self_address& self, size_t timeout = DEFAULT_TIMEOUT);

	tcp_connection(const tcp_connection& other) = delete;

	tcp_connection(tcp_connection&& other);

	tcp_connection& operator=(const tcp_connection& other) = delete;

	tcp_connection& operator=(tcp_connection&& other);

	~tcp_connection();

	/// Returns true if the socket is ready to use; false otherwise.
	bool valid() const;

	/// Sends 'msg' through TCP and waits for a response.
	/// Returns a tcp stream with the response.
	stream<tcp_source> request(const out_stream& msg) const;

	/// Returns a tcp stream with the underlying socket as source.
	net::stream<net::tcp_source> to_stream() const;

	/// Sends 'msg' to the tcp peer.
	void answer(const out_stream& msg) const;
protected:
	int _fd{-1};
};

/// Represents a passive tcp socket.
struct tcp_server : protected tcp_connection {
	tcp_server(const self_address& self, size_t sub_conns = DEFAULT_LISTEN_CONNS);

	/// Accepts a new connection and creates a new tcp_connection
	/// for it.
	tcp_connection accept_client(other_address& other);

	/// Returns true if the socket is ready to use; false otherwise.
	bool valid() const;

	/// Returns the underlying file descriptor.
	/// CLosing the returned file descriptor is undefined behaviour.
	int get_fildes();
};

/// Returns true if the plid is valid; false otherwise.
bool is_valid_plid(const field& field);

/// Returns true if the duration is valid; false otherwise.
bool is_valid_max_playtime(const field& field);

/// Returns true if the color is valid; false otherwise.
bool is_valid_color(const field& field);

/// Returns true if the filename is valid; false otherwise.
bool is_valid_fname(const field& field);

/// Returns true if the file size is valid; false otherwise.
bool is_valid_fsize(size_t fsize);

/// Maps keywords to functions (actions).
template<typename SOURCE, typename... ARGS>
struct action_map {
	using arg_stream = stream<SOURCE>;
	using action = std::function<void(arg_stream&, ARGS...)>;

	/// Adds an action that's triggered by the keyword 'name'.
	void add_action(const std::string_view& name, const action& action) {
		_actions.insert({std::move(static_cast<std::string>(name)), action});
	}

	/// Adds an action triggered by each of the listed 'names'.
	void add_action(std::initializer_list<const std::string_view> names, const action& action) {
		for (auto name : names)
			add_action(name, action);
	}

	/// Executes the action associated to the first keyword
	/// read in the given stream.
	void execute(arg_stream& strm, ARGS&&... args) const {
		std::string comm = strm.read(1, SIZE_MAX);
		auto it = _actions.find(comm);
		if (it == _actions.end())
			throw syntax_error{"Unknown action"};
		return it->second(strm, std::forward<ARGS>(args)...);
	}
private:
	std::unordered_map<std::string, action> _actions;
};
};

#endif
