#include "common.hpp"

using namespace net;

message::iterator::iterator(const message& message, char delimiter) : _parent{message}, _delimiter{delimiter} {
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

void message::iterator::operator++() {
	_from = _to;
	_to++;
	if (_in_del_phase) {
		for (; _to < _parent._raw.length() && _parent._raw[_to] != _delimiter; _to++);
		_in_del_phase = false;
		return;
	}
	for (; _to < _parent._raw.length() && _parent._raw[_to] == _delimiter; _to++);
	_in_del_phase = true;
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

message::iterator message::begin(char delimiter) const {
	return iterator{*this, delimiter};
}

size_t message::end() const {
	return _raw.size();
}

const std::string& message::data() const {
	return _raw;
}

action_map& action_map::add_action(const std::string& name, const action& action) {
	_actions.insert({name, action});
	return *this;
}

int action_map::execute(const std::string& command) const {
	message msg{command};
	auto field_it = msg.begin();
	if (field_it == msg.end())
		return 1;
	if (field_it.is_in_delimiter_phase()) {
		++field_it;
		if (field_it == msg.end())
			return 1;
	}
	auto it = _actions.find(static_cast<std::string>(*field_it));
	if (it == _actions.end())
		return 1;
	return it->second(msg);
}
