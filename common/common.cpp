#include "common.hpp"

#include <stdexcept>
#include <iostream>

using namespace net;

self_address::self_address(const std::string_view& other_addr, const std::string_view& other_port, int socktype, int family)
	: _fam{family}, _sockt{socktype}, _passive{false} {
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = socktype;
	hints.ai_family = family;

	if (getaddrinfo(other_addr.data(), other_port.data(), &hints, &_info) != 0) {
		_info = nullptr;
		return;
	}
}

self_address::self_address(const std::string_view& other_port, int socktype, int family)
	: _fam{family}, _sockt{socktype}, _passive{true} {
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = socktype;
	hints.ai_family = family;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(NULL, other_port.data(), &hints, &_info) != 0) {
		_info = nullptr;
		return;
	}
}

self_address::self_address(self_address&& other) :
	_info{other._info}, _fam{other._fam}, _sockt{other._sockt}, _passive{other._passive} {
	other._info = nullptr;
}

self_address& self_address::operator=(self_address&& other) {
	if (this == &other)
		return *this;
	if (_info)
		freeaddrinfo(_info);
	_info = other._info;
	_fam = other._fam;
	_sockt = other._sockt;
	_passive = other._passive;
	other._info = nullptr;
	return *this;
}

self_address::~self_address() {
	if (!_info)
		return;
	freeaddrinfo(_info);
}

bool self_address::valid() const {
	return _info;
}

const addrinfo* self_address::unwrap() const {
	return _info;
}

int self_address::family() const {
	return _fam;
}

int self_address::socket_type() const {
	return _sockt;
}

bool self_address::is_passive() const {
	return _passive;
}

udp_connection::udp_connection(self_address&& self, size_t timeout) : _self{std::move(self)} {
	if (!_self.valid() || _self.socket_type() != SOCK_DGRAM)
		return;
	if ((_fd = socket(_self.family(), _self.socket_type(), 0)) == -1)
		return;
	if (_self.is_passive()) {
		if (bind(_fd, _self.unwrap()->ai_addr, _self.unwrap()->ai_addrlen) == -1) {
			close(_fd);
			_fd = -1;
		}
		return;
	}
	timeval t;
	t.tv_sec = timeout; // s second timeout
	t.tv_usec = 0;
	if (setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) == -1) {
		close(_fd);
		_fd = -1;
		return;
	}
}

udp_connection::udp_connection(udp_connection&& other)
	: _self{std::move(other._self)}, _fd{other._fd} {
	std::copy(other._buf, other._buf + UDP_MSG_SIZE, _buf);
	other._fd = -1;
}

udp_connection& udp_connection::operator=(udp_connection&& other) {
	if (this == &other)
		return *this;
	if (_fd != -1)
		close(_fd);
	_self = std::move(other._self);
	_fd = other._fd;
	std::copy(other._buf, other._buf + UDP_MSG_SIZE, _buf);
	other._fd = -1;
	return *this;
}

udp_connection::~udp_connection() {
	if (_fd != -1)
		close(_fd);
}

bool udp_connection::valid() const {
	return _fd != -1;
}

std::pair<action_status, stream<udp_source>> udp_connection::request(const out_stream& msg, other_address& other) {
	auto to_send = msg.view();
	other.addrlen = sizeof(other.addr);
	for (int retries = 0; retries < MAX_RESEND; retries++) {
		int n = sendto(_fd, to_send.data(), to_send.size(), 0, _self.unwrap()->ai_addr, _self.unwrap()->ai_addrlen);
		if (n == -1)
			return {action_status::SEND_ERR, {std::string_view{}}};
		n = recvfrom(_fd, _buf, UDP_MSG_SIZE, 0, (struct sockaddr*) &other.addr, &other.addrlen);
		if (n >= 0) {
			std::cerr << "UDP Response: \"" << std::string_view{_buf, static_cast<size_t>(n)} << "\"" <<std::endl;
			return {action_status::OK, {std::string_view{_buf, static_cast<size_t>(n)}}};
		}
		n = -1;
		if (errno != EWOULDBLOCK && errno != EAGAIN)
			return {action_status::RECV_ERR, {std::string_view{}}};
	}
	return {action_status::CONN_TIMEOUT, {std::string_view{}}};
}

action_status udp_connection::answer(const out_stream& msg, const other_address& other) const {
	auto to_send = msg.view();
	int n = sendto(_fd, to_send.data(), to_send.size(), 0, (struct sockaddr*) &other.addr, other.addrlen);
	if (n == -1)
		return action_status::SEND_ERR;
	return action_status::OK;
}

std::pair<action_status, stream<udp_source>> udp_connection::listen(other_address& other) {
	other.addrlen = sizeof(other.addr);
	int n = recvfrom(_fd, _buf, UDP_MSG_SIZE, 0, (struct sockaddr*) &other.addr, &other.addrlen);
	if (n == -1)
		return {action_status::RECV_ERR, {std::string_view{}}};
	return {action_status::OK, {std::string_view{_buf, static_cast<size_t>(n)}}};
}

int udp_connection::get_fildes() {
	return _fd;
}

tcp_connection::tcp_connection() : _fd{-1} {}

tcp_connection::tcp_connection(int fd) : _fd{fd} {
	if (fd < 0)
		_fd = -1;
}

tcp_connection::tcp_connection(const self_address& self) {
	if (!self.valid() || self.socket_type() != SOCK_STREAM)
		return;
	if ((_fd = socket(self.family(), self.socket_type(), 0)) == -1)
		return;
	if (!self.is_passive() && connect(_fd, self.unwrap()->ai_addr, self.unwrap()->ai_addrlen) == -1) {
		close(_fd);
		_fd = -1;
		return;
	}
}

tcp_connection::tcp_connection(tcp_connection&& other) : _fd{other._fd} {
	other._fd = -1;
}

tcp_connection& tcp_connection::operator=(tcp_connection&& other) {
	if (this == &other)
		return *this;
	if (_fd != -1)
		close(_fd);
	_fd = other._fd;
	other._fd = -1;
	return *this;
}

tcp_connection::~tcp_connection() {
	if (_fd != -1)
		close(_fd);
}

bool tcp_connection::valid() const {
	return _fd != -1;
}

std::pair<action_status, stream<tcp_source>> tcp_connection::request(const out_stream& msg) const {
	action_status status = answer(msg);
	if (status != action_status::OK)
		return {status, {-1}};
	return {action_status::OK, {_fd}};
}

stream<tcp_source> tcp_connection::to_stream() const {
	return stream<tcp_source>{_fd};
}

action_status tcp_connection::answer(const out_stream& out) const {
	size_t done = 0;
	auto view = out.view();
	while (done < view.size()) {
		int n = write(_fd, view.data() + done, view.size() - done);
		if (n <= 0)
			return action_status::SEND_ERR;
		done += n;
	}
	return action_status::OK;
}

tcp_server::tcp_server(const self_address& self, size_t sub_conns) : tcp_connection{self} {
	if (!self.is_passive())
		return;
	if (bind(_fd, self.unwrap()->ai_addr, self.unwrap()->ai_addrlen) == -1
		|| listen(_fd, sub_conns) == -1) {
		close(_fd);
		_fd = -1;
		return;
	}
}

std::pair<action_status, tcp_connection> tcp_server::accept_client(other_address& other) {
	int new_fd = -1;
	other.addrlen = sizeof(other.addr);
	if ((new_fd = accept(_fd, (sockaddr*) &other.addr, &other.addrlen)) == -1)
		return {net::action_status::CONN_ERR, tcp_connection{}};
	tcp_connection new_conn{new_fd};
	if (!new_conn.valid())
		return {net::action_status::CONN_ERR, tcp_connection{}};
	return {net::action_status::OK, std::move(new_conn)};
}

bool tcp_server::valid() const {
	return tcp_connection::valid();
}

int tcp_server::get_fildes() {
	return _fd;
}

bool source::found_eom() const {
	return _found_eom;
}

bool source::finished() const {
	return _finished;
}

void source::reset() {
	_finished = false;
	_found_eom = false;
}

file_source::file_source(int fd) : _fd{fd} {}

bool file_source::is_skippable(char c) const {
	return std::isspace(c) && c != DEFAULT_EOM;
}

action_status file_source::read_len(std::string& buf, size_t len, size_t& n, bool check_eom) {
	n = 0;
	if (len == 0)
		return action_status::OK;
	if (_finished) {
		if (_found_eom)
			return net::action_status::OK;
		return net::action_status::MISSING_EOM;
	}
	char temp[len];
	while (len != 0) {
		int res = read(_fd, temp, len);
		if (res < 0)
			return action_status::CONN_TIMEOUT;
		if (res == 0) {
			_finished = true;
			return net::action_status::MISSING_EOM;
		}
		len -= res;
		if (check_eom && temp[res - 1] == DEFAULT_EOM) {
			res--;
			len = 0;
			_finished = true;
			_found_eom = true;
		}
		buf.append(temp, temp + res);
		n += res;
	}
	return action_status::OK;
}

tcp_source::tcp_source(int fd) : net::file_source{fd} {}

bool tcp_source::is_skippable(char c) const {
	return c == DEFAULT_SEP;
}

string_source::string_source(const std::string_view& source) : _source(source) {}

string_source::string_source(std::string_view&& source) : _source(std::move(source)) {}

action_status string_source::read_len(std::string& buf, size_t len, size_t& n, bool check_eom) {
	n = 0;
	if (len == 0)
		return action_status::OK;
	if (_finished) {
		if (_found_eom)
			return net::action_status::OK;
		return net::action_status::MISSING_EOM;
	}
	size_t end = _at + len;
	if (end > _source.size()) {
		end = _source.size();
		_finished = true;
		if (_source.back() != DEFAULT_EOM)
			return net::action_status::MISSING_EOM;
	}
	buf.append(std::begin(_source) + _at, std::begin(_source) + end);
	n = end - _at;
	_at = end;
	if (check_eom && buf.back() == DEFAULT_EOM) {
		n--;
		buf.pop_back();
		_found_eom = true;
		_finished = true;
	}
	return net::action_status::OK;
}

bool string_source::is_skippable(char c) const {
	return std::isspace(c) && c != DEFAULT_EOM;
}

udp_source::udp_source(const std::string_view& source) : string_source(source) {}
udp_source::udp_source(std::string_view&& source) : string_source(std::move(source)) {}

bool udp_source::is_skippable(char c) const {
	return c == DEFAULT_SEP;
}

out_stream& out_stream::write(const field& f) {
	if (_primed)
		_primed = false;
	_buf.append(std::begin(f), std::end(f));
	_buf.push_back(DEFAULT_SEP);
	return *this;
}

out_stream& out_stream::write(char c) {
	if (_primed)
		_primed = false;
	_buf.push_back(c);
	_buf.push_back(DEFAULT_SEP);
	return *this;
}

out_stream& out_stream::write_and_fill(const field& f, size_t n, char fill) {
	if (_primed)
		_primed = false;
	if (n > f.size())
		_buf.insert(_buf.size(), n - f.size(), fill);
	_buf.append(std::begin(f), std::end(f));
	_buf.push_back(DEFAULT_SEP);
	return *this;
}

out_stream& out_stream::prime() {
	if (_primed)
		return *this;
	if (!_buf.empty())
		_buf.back() = DEFAULT_EOM;
	else
		_buf.push_back(DEFAULT_EOM);
	return *this;
}

const std::string_view out_stream::view() const {
	return _buf;
}

std::string net::status_to_message(action_status status) {
	std::string res = "get_error_message failed";
	switch (status) {
		case action_status::OK:
			res = "No error";
			break;

		case action_status::UNK_ACTION:
			res = "Requested command does not exist";
			break;

		case action_status::RET_ERR:
			res = "Server received unexpected request";
			break;

		case action_status::MISSING_ARG:
			res = "Missing arguments for requested command";
			break;

		case action_status::EXCESS_ARGS:
			res = "Excess arguments for requested command";
			break;

		case action_status::BAD_ARG:
			res = "Ilegal argument for requested command";
			break;

		case action_status::ERR:
			res = "Message did not match the expected format";
			break;

		case action_status::ONGOING_GAME:
			res = "Requested command can only be called after finishing the current game";
			break;

		case action_status::NOT_IN_GAME:
			res = "Requested command can only be called after starting a game";
			break;

		case action_status::SEND_ERR:
			res = "Failed to send message to the server. Please check the server address and port, or try again later";
			break;

		case action_status::CONN_TIMEOUT:
			res = "Connection timed out";
			break;

		case action_status::RECV_ERR:
			res = "Failed to receive a response from the server"; 
			break;

		case action_status::MISSING_EOM:
			res = "Message did not end with \"end of message\" (EOM)";
			break;

		case action_status::UNK_REPLY:
			res = "Unknown reply in received answer";
			break;

		case action_status::UNK_STATUS:
			res = "Unknown status in received answer";
			break;

		case action_status::START_NOK:
			res = "User with the given player ID is already in-game";
			break;

		case action_status::START_ERR:
			res = "Malformed request: either the syntax, player ID or time were incorrect";
			break;

		case action_status::DEBUG_ERR:
			res = "Malformed request: either the syntax, player ID, time or color code were incorrect";
			break;
		case action_status::QUIT_EXIT_ERR:
			res = "Malformed request: either the syntax or player ID were incorrect";
			break;
		case action_status::TRY_NT:
			res = "Trial number mismatch, trials Resynchronized";
			break;
		case action_status::TRY_ERR:
			res = "Malformed request: either the syntax, player ID or the colour code were incorrect";
			break;
		case action_status::TRY_DUP:
			res = "Duplicated guess. Try a different guess";
			break;
		case action_status::TRY_INV:
			res = "Invalid trial, the trial number isn't the expected value";
			break;
		case action_status::TRY_NOK:
			res = "Trial out of context (possibly no ongoing game)";
			break;
		case action_status::TRY_ENT:
			res = "Maximum number of attemps achieved (8), you lost the game";
			break;
		case action_status::TRY_ETM:
			res = "Maximum play time achieved, you lost the game";
			break;
		case action_status::CONN_ERR:
			res = "Could not connect to server (check address and port)";
			break;
		case action_status::PERSIST_ERR:
			res = "Could not read/write a file";
			break;
		case action_status::FS_ERR:
			res = "Failed to open/close a file";
			break;
		case action_status::NOT_FOUND:
			res = "Did not find requested file";
			break;
		default:
			res = "Unknown error";
	}
	return res;
}

action_status net::is_valid_plid(const field& field) {
	if (field.length() != PLID_SIZE) // PLID has 6 digits
		return net::action_status::BAD_ARG;
	for (char c : field)
		if (c < '0' || c > '9')
			return net::action_status::BAD_ARG;
	return net::action_status::OK;
}

action_status net::is_valid_max_playtime(const field& field) {
	if (field.length() > 3) // avoid out_of_range exception
		return net::action_status::BAD_ARG;
	int max_playtime = -1;
	try {
		max_playtime = std::stoi(std::string(field));
	} catch (const std::invalid_argument& err) { // cannot be read
		return net::action_status::BAD_ARG;
	} // out_of_range exception shouldn't be an issue
	if (max_playtime < 0 || max_playtime > MAX_PLAYTIME)
		return net::action_status::BAD_ARG;
	return net::action_status::OK;
}

action_status net::is_valid_color(const field& field) {
	if (field.length() != 1)
        return action_status::BAD_ARG;
	for (auto col : VALID_COLORS)
		if (field[0] == col)
			return action_status::OK;
    return action_status::BAD_ARG;
}
