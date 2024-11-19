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
