#include "common.hpp"

#include <stdexcept>

using namespace net;

socket_context::socket_context(const std::string_view& rec_addr, const std::string_view& rec_port, int type, size_t timeout) {
	socket_fd = socket(AF_INET, type, 0);
	if (socket_fd == -1)
		return;
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = type;

	if (getaddrinfo(rec_addr.data(), rec_port.data(), &hints, &receiver_info) != 0) {
		close(socket_fd);
		socket_fd = -1;
		return;
	}

	if (type == SOCK_STREAM && connect(socket_fd, receiver_info->ai_addr, receiver_info->ai_addrlen) != 0) {
		freeaddrinfo(receiver_info);
		close(socket_fd);
		socket_fd = -1;
		return;
	}
	
	if (timeout == 0)
		return;

	timeval t;
	t.tv_sec = timeout; // s second timeout
	t.tv_usec = 0;
	if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) == -1) {
		freeaddrinfo(receiver_info);
		close(socket_fd);
		socket_fd = -1;
		return;
	}
}

socket_context::socket_context(const std::string_view& rec_port, int type, size_t timeout) {
	socket_fd = socket(AF_INET, type, 0);
	if (socket_fd == -1)
		return;
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = type;
	hints.ai_flags = SOCK_DGRAM;

	if (getaddrinfo(NULL, rec_port.data(), &hints, &receiver_info) != 0) {
		close(socket_fd);
		socket_fd = -1;
		return;
	}

	if (bind(socket_fd, receiver_info->ai_addr, receiver_info->ai_addrlen) != 0) {
		close(socket_fd);
		socket_fd = -1;
		return;
	}

	if (type == SOCK_STREAM && listen(socket_fd, DEFAULT_LISTEN_CONNS) != 0) {
		freeaddrinfo(receiver_info);
		close(socket_fd);
		socket_fd = -1;
		return;
	}
	
	if (timeout == 0)
		return;

	timeval t;
	t.tv_sec = timeout; // s second timeout
	t.tv_usec = 0;
	if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) == -1) {
		freeaddrinfo(receiver_info);
		close(socket_fd);
		socket_fd = -1;
		return;
	}
}

socket_context::~socket_context() {
	if (socket_fd == -1)
		return;
	freeaddrinfo(receiver_info);
	close(socket_fd);
}

bool socket_context::is_valid() {
	return socket_fd != -1;
}

bool source::found_eom() const {
	return _found_eom;
}

bool source::finished() const {
	return _finished;
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
			res = "Could not write the file to disk";
			break;
		default:
			res = "Unknown error";
	}
	return res;
}

action_status net::udp_request(const out_stream& out_str, net::socket_context& udp_info, char* ans, uint32_t ans_sz, int& read) {
	auto msg = out_str.view();
	for (int retries = 0; retries < MAX_RESEND; retries++) {
		read = sendto(udp_info.socket_fd, msg.data(), msg.size(), 0, udp_info.receiver_info->ai_addr, udp_info.receiver_info->ai_addrlen);
		if (read == -1)
			return net::action_status::SEND_ERR;
		read = recvfrom(udp_info.socket_fd, ans, ans_sz, 0, (struct sockaddr*) &udp_info.sender_addr, &udp_info.sender_addr_len);
		if (read >= 0)
			return net::action_status::OK;
		if (errno != EWOULDBLOCK && errno != EAGAIN)
			return net::action_status::RECV_ERR;
	}
	return net::action_status::CONN_TIMEOUT;
}

std::pair<action_status, stream<tcp_source>> net::tcp_request(const out_stream& out_str, net::socket_context& tcp_info) {
	int done = 0;
	auto view = out_str.view();
	while (done < view.size()) {
		int n = write(tcp_info.socket_fd, view.data() + done, view.size() - done);
		if (n <= 0)
			return {action_status::SEND_ERR, {-1}};
		done += n;
	}
	return {net::action_status::OK, {tcp_info.socket_fd}};
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
	if (max_playtime < 0 || max_playtime > 600)
		return net::action_status::BAD_ARG;
	return net::action_status::OK;
}

void net::fill_max_playtime(char res[MAX_PLAYTIME_SIZE], const field& max_playtime) {
	int res_i = MAX_PLAYTIME_SIZE - 1;
	for (int i = std::size(max_playtime) - 1; i > -1; i--, res_i--)
		res[res_i] = max_playtime[i];
	for (; res_i > -1; res_i--)
		res[res_i] = '0';
}

action_status net::is_valid_color(const field& field) {
	if (field.length() != 1)
		return net::action_status::BAD_ARG;
	action_status res;
	switch (field[0]) {
		case 'R':
		case 'G':
		case 'B':
		case 'Y':
		case 'O':
		case 'P':
			res = net::action_status::OK;
			break;

		default:
			res = net::action_status::BAD_ARG;
	}
	return res;
}
