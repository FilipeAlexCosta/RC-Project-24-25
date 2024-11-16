#include "common.hpp"

using namespace net;

message::message(const std::string& msg) : _raw{msg} {
	if (_raw.length() == 0) {
		_to = 1;
		return;
	}
	if (_raw[_from] == DEL) {
		for (; _to < _raw.length() && _raw[_to] == DEL; _to++);
		_in_del_phase = true;
		return;
	}
	for (; _to < _raw.length() && _raw[_to] != DEL; _to++);
	_in_del_phase = false;
}

void message::next_field() {
	_from = _to;
	_to++;
	if (_in_del_phase) {
		for (; _to < _raw.length() && _raw[_to] != DEL; _to++);
		_in_del_phase = false;
		return;
	}
	for (; _to < _raw.length() && _raw[_to] == DEL; _to++);
	_in_del_phase = true;
}

bool message::is_in_delimiter_phase() const {
	return _in_del_phase;
}

bool message::has_next() const {
	return _to <= _raw.length();
}

std::string message::extract_field() const {
	return std::string(_raw.begin() + _from, _raw.begin() + _to);
}

message::field message::begin() const {
	return field(*this);
}

message::field message::end() const {
	return field(*this, _to - _from);
}

message::reverse_field message::rbegin() const {
	return reverse_field(*this);
}

message::reverse_field message::rend() const {
	return reverse_field(*this, _to - _from);
}

message::field::field(const message& parent, ssize_t index)
	: _parent{parent}, _idx{index} {
	_idx += _parent._from;
}

void message::field::operator++(int) {
	_idx++;
}

void message::field::operator++() {
	_idx++;
}

char message::field::operator[](int idx) const {
	return _parent._raw[_parent._from + idx];
}

char message::field::operator*() const {
	return _parent._raw[_idx];
}

bool message::field::operator!=(const field& other) const {
	return _idx != other._idx;
}

size_t message::field::length() const {
	return _parent._to - _parent._from;
}

message::reverse_field::reverse_field(const message& parent, ssize_t index)
	: field(parent, index) {
	_idx = _parent._to - index - 1;
}

void message::reverse_field::operator++(int) {
	_idx--;
}

void message::reverse_field::operator++() {
	_idx--;
}

char message::reverse_field::operator[](int idx) const {
	return _parent._raw[_parent._to - 1 - idx];
}

action_map& action_map::add_action(const std::string& name, const std::function<int(message&)>& action) {
	_actions.insert({name, action});
	return *this;
}

int action_map::execute(const std::string& command) const {
	message msg{command};
	if (!msg.has_next())
		return 1;
	if (msg.is_in_delimiter_phase())
		msg.next_field();
	auto it = _actions.find(msg.extract_field());
	if (it == _actions.end())
		return 1;
	return it->second(msg);
}
