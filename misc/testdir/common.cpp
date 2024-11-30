#include "common.hpp"

#include <stdexcept>

using namespace net;

socket_context::socket_context(const std::string_view& rec_addr, const std::string_view& rec_port, int type) {
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

	if (type == SOCK_STREAM && connect(socket_fd, receiver_info->ai_addr, receiver_info->ai_addrlen) == -1) {
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

int socket_context::set_timeout(size_t s) {
	struct timeval timeout;
	timeout.tv_sec = s; // s second timeout
	timeout.tv_usec = 0;
	return setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1;
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
		default:
			res = "Unknown error";
	}
	return res;
}

action_status net::udp_request(const char* req, uint32_t req_sz, net::socket_context& udp_info, char* ans, uint32_t ans_sz, int& read) {
	for (int retries = 0; retries < MAX_RESEND; retries++) {
		read = sendto(udp_info.socket_fd, req, req_sz, 0, udp_info.receiver_info->ai_addr, udp_info.receiver_info->ai_addrlen);
		if (read == -1)
			return net::action_status::SEND_ERR;
		read = recvfrom(udp_info.socket_fd, ans, ans_sz, 0, (struct sockaddr*) &udp_info.sender_addr, &udp_info.sender_addr_len);
		if (read >= 0) {
			if (read == 0 || ans[--read] != DEFAULT_EOM)
				return net::action_status::MISSING_EOM;
			return net::action_status::OK;
		}
		if (errno != EWOULDBLOCK && errno != EAGAIN)
			return net::action_status::RECV_ERR;
	}
	return net::action_status::CONN_TIMEOUT;
}

action_status net::tcp_request(const char* req, uint32_t req_sz, net::socket_context& tcp_info, char* ans, uint32_t ans_sz, int& r) {
	int n = 0;
	while (r < req_sz) {
		n = write(tcp_info.socket_fd, req, req_sz - r);
		if (n <= 0)
			return net::action_status::SEND_ERR;
		r += n;
		req += n;
	}

	while (r < ans_sz) {
		n = read(tcp_info.socket_fd, ans, ans_sz - r);
		if (n <= 0)
			return net::action_status::SEND_ERR;
		r += n;
		ans += n;
	}

	return net::action_status::OK;
}

std::pair<action_status, message> net::get_fields(const char* buf, size_t buf_sz, std::initializer_list<int> field_szs) {
	message fields;
	fields.reserve(std::size(field_szs));
	size_t i = 0;
	for (; i < buf_sz && std::isspace(buf[i]); i++);
	for (auto size : field_szs) {
		if (size < 0) {
			for (; i < buf_sz && std::isspace(buf[i]); i++);
			size = 1;
			for (; (size + i) < buf_sz && (!std::isspace(buf[size + i])); size++);
		}
		size_t next_sep = i + size;
		if (next_sep >= buf_sz) {
			if (next_sep != buf_sz)
				return {net::action_status::MISSING_ARG, fields};
			fields.push_back(std::string_view(buf + i, size));
			i = next_sep + 1;
			continue;
		}
		if (!std::isspace(buf[next_sep]))
			return {net::action_status::BAD_ARG, fields};
		fields.push_back(std::string_view(buf + i, size));
		i = next_sep + 1;
		for (; i < buf_sz && std::isspace(buf[i]); i++);
	}
	if (i < buf_sz)
		return {net::action_status::EXCESS_ARGS, fields};
	return {net::action_status::OK, fields};
}

std::pair<action_status, message> net::get_fields_strict(const char* buf, size_t buf_sz, std::initializer_list<int> field_szs, char sep) {
	message fields;
	fields.reserve(std::size(field_szs));
	size_t i = 0;
	if (buf_sz > 0 && buf[0] == sep)
		return {net::action_status::ERR, fields};
	for (auto size : field_szs) {
		if (size < 0) {
			size = 0;
			for (; (size + i) < buf_sz && buf[size + i] != sep; size++);
		}
		size_t next_sep = i + size;
		if (next_sep >= buf_sz) {
			if (next_sep != buf_sz)
				return {net::action_status::MISSING_ARG, fields};
			fields.push_back(std::string_view(buf + i, size));
			i = next_sep + 1;
			continue;
		}
		if (buf[next_sep] != sep)
			return {net::action_status::BAD_ARG, fields};
		fields.push_back(std::string_view(buf + i, size));
		i = next_sep + 1;
	}
	if (i <= buf_sz)
		return {net::action_status::ERR, fields};
	return {net::action_status::OK, fields};
}

int net::prepare_buffer(char* buf, int buf_sz, message msg, char sep, char eom) {
	uint32_t i = 0;
	size_t fld_i = 0;
	for (; fld_i < msg.size() - 1; fld_i++) {
		if (i + std::size(msg[fld_i]) >= buf_sz)
			return - 1;
		for (char c : msg[fld_i])
			buf[i++] = c;
		buf[i++] = sep;
	}
	if (std::size(msg) != 0 && i + std::size(msg[fld_i]) >= buf_sz)
		return - 1;
	for (char c : msg[fld_i])
		buf[i++] = c;
	buf[i++] = eom;
	return i;
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
	/*if (max_playtime < 0 || max_playtime > 600) // check <= 600
		return net::action_status::BAD_ARG;*/
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
