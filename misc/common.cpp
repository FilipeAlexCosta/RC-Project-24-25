#include "common.hpp"

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

std::pair<action_status, std::vector<std::string_view>> net::get_fields(char* buf, size_t buf_sz, std::initializer_list<int> field_szs, char sep) {
	std::vector<std::string_view> fields;
	fields.reserve(std::size(field_szs));
	size_t i = 0;
	for (; i < buf_sz && buf[i] == sep; i++);
	for (auto size : field_szs) {
		if (size < 0) {
			for (; i < buf_sz && buf[i] == sep; i++);
			size = 1;
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
		for (; i < buf_sz && buf[i] == sep; i++);
	}
	if (i < buf_sz)
		return {net::action_status::EXCESS_ARGS, fields};
	return {net::action_status::OK, fields};
}

std::pair<action_status, std::vector<std::string_view>> net::get_fields_strict(char* buf, size_t buf_sz, std::initializer_list<uint32_t> field_szs, char sep) {
	std::vector<std::string_view> fields;
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

message::iterator::iterator(const message& message, char separator) : _parent{message}, _delimiter{separator} {
	if (_parent._raw.length() == 0) {
		_to = 1;
		return;
	}
	if (_parent._raw[_from] == _delimiter) {
		for (; _to < _parent._raw.length() && _parent._raw[_to] == _delimiter; _to++);
		_in_del_phase = true;
		return;
	}
	for (; _to < _parent._raw.length() && _parent._raw[_to] != _delimiter; _to++);
	_in_del_phase = false;
}

message::iterator& message::iterator::operator++() {
	_from = _to;
	_to++;
	if (_in_del_phase) {
		for (; _to < _parent._raw.length() && _parent._raw[_to] != _delimiter; _to++);
		_in_del_phase = false;
		return *this;
	}
	for (; _to < _parent._raw.length() && _parent._raw[_to] == _delimiter; _to++);
	_in_del_phase = true;
	return *this;
}

bool message::iterator::operator!=(size_t other) const {
	return (_to - 1) != other;
}

bool message::iterator::operator==(size_t other) const {
	return (_to - 1) == other;
}

message::iterator::field message::iterator::operator*() const {
	return field{_parent._raw.data() + _from, _to - _from};
}

bool message::iterator::is_in_delimiter_phase() const {
	return _in_del_phase;
}

message::message::message(const std::string& msg) : _raw{msg} {}

message::iterator message::begin(char separator) const {
	return iterator{*this, separator};
}

size_t message::end() const {
	return _raw.size();
}

const std::string& message::data() const {
	return _raw;
}

void action_map::add_action(const std::string_view& name, const action& action) {
	_actions.insert({std::move(static_cast<std::string>(name)), action});
}

void action_map::add_action(std::initializer_list<const std::string_view> names, const action& action) {
	for (auto name : names)
		add_action(name, action);
}

action_status action_map::execute(const std::string& command) const {
	message msg{command};
	auto field_it = msg.begin();
	if (field_it == msg.end())
		return action_status::UNK_ACTION;
	if (field_it.is_in_delimiter_phase()) {
		++field_it;
		if (field_it == msg.end())
			return action_status::UNK_ACTION;
	}
	auto it = _actions.find(static_cast<std::string>(*field_it));
	if (it == _actions.end())
		return action_status::UNK_ACTION;
	return it->second(msg);
}
