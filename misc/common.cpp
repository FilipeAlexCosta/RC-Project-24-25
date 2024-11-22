#include "common.hpp"

#include <stdexcept>

using namespace net;

std::string net::status_to_message(action_status status) {
	std::string res = "get_error_message failed";
	switch (status) {
		case action_status::OK:
			res = "No error";
			break;

		case action_status::UNK_ACTION:
			res = "Requested command does not exist";
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

		default:
			res = "Unknown error";
	}
	return res;
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

std::pair<action_status, message> net::get_fields_strict(const char* buf, size_t buf_sz, std::initializer_list<uint32_t> field_szs, char sep) {
	message fields;
	fields.reserve(std::size(field_szs));
	size_t i = 0;
	if (buf_sz > 0 && buf[0] == sep)
		return {net::action_status::ERR, fields};
	for (auto size : field_szs) {
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
	if (max_playtime < 0 || max_playtime > 600) // check <= 600
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

void action_map::add_action(const std::string_view& name, const action& action) {
	_actions.insert({std::move(static_cast<std::string>(name)), action});
}

void action_map::add_action(std::initializer_list<const std::string_view> names, const action& action) {
	for (auto name : names)
		add_action(name, action);
}

action_status action_map::execute(const std::string& command) const {
	size_t from  = 0;
	for (; from < command.size() && std::isspace(command[from]); from++);
	if (from > command.size())
		return action_status::UNK_ACTION;
	size_t to = from + 1;
	for (; to < command.size() && (!std::isspace(command[to])); to++);
	auto it = _actions.find(std::string(std::begin(command) + from, std::begin(command) + to));
	if (it == _actions.end())
		return action_status::UNK_ACTION;
	return it->second(command);
}
