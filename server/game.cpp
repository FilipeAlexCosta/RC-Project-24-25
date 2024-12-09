#include "game.hpp"

game::game(uint16_t duration) : _duration{duration}, _debug{false} {
	for (int i = 0; i < GUESS_SIZE; i++)
		_secret_key[i] = net::VALID_COLORS[std::rand() % std::size(net::VALID_COLORS)];
}

game::game(uint16_t duration, const char secret_key[GUESS_SIZE])
	: _duration{duration}, _debug{true} {
	std::copy(secret_key, secret_key + GUESS_SIZE, _secret_key);
}

game::result game::guess(char play[GUESS_SIZE]) {
	if (_curr_trial > '0' && last_trial()->nB == GUESS_SIZE)
		return result::WON;
	if (_curr_trial >= MAX_TRIALS)
		return result::LOST_TRIES; // ensures calls after finishing are correct
	if (_start + _duration < std::time(nullptr))
		return result::LOST_TIME;
	for (int j = 0; j < GUESS_SIZE; j++)
		_trials[_curr_trial - '0'].trial[j] = play[j];
	auto [nB, nW] = compare(play);
	_trials[_curr_trial - '0'].nB = nB;
	_trials[_curr_trial - '0'].nW = nW;
	_curr_trial++;
	if (nB == GUESS_SIZE)
		return result::WON;
	if (_curr_trial >= MAX_TRIALS) // actual check
		return result::LOST_TRIES;
	return result::ONGOING;
}

std::pair<uint8_t, uint8_t> game::compare(const char guess[GUESS_SIZE]) {
	uint8_t nB = 0, nW = 0;
	for (int real_it = 0; real_it < GUESS_SIZE; real_it++) {
		if (_secret_key[real_it] == guess[real_it]) {
			nB++;
			continue;
		}
		bool done = false;
		for (int guess_it = 0; guess_it < real_it; guess_it++) {
			if (_secret_key[real_it] == guess[guess_it]) {
				done = true;
				break;
			}
		}
		if (done) {
			nW++;
			continue;
		}
		for (int guess_it = real_it + 1; guess_it < GUESS_SIZE; guess_it++) {
			if (_secret_key[real_it] == guess[guess_it]) {
				nW++;
				break;
			}
		}
	}
	return {nB, nW};
}

void game::undo_guess() {
	if (_curr_trial == '0')
		return;
	_curr_trial--;
}

game::result game::has_ended() const {
	if (_curr_trial > '0' && last_trial()->nB == GUESS_SIZE)
		return result::WON;
	if (_curr_trial >= MAX_TRIALS)
		return result::LOST_TRIES;
	if (_start + _duration < std::time(nullptr))
		return result::LOST_TIME;
	return result::ONGOING;
}

const char* game::secret_key() const {
	return _secret_key;
}

char game::current_trial() const {
	return _curr_trial;
}

char game::is_duplicate(const char guess[GUESS_SIZE]) const {
	for (int i = 0; i < MAX_TRIALS - '0'; i++) {
		int j = 0;
		for (; j < GUESS_SIZE; j++)
			if (_trials[i].trial[j] != guess[j])
				break;
		if (j == GUESS_SIZE)
			return i + '0' + 1;
	}
	return MAX_TRIALS + 1;
}

const trial_record* game::last_trial() const {
	if (_curr_trial == '0')
		return nullptr;
	return &_trials[_curr_trial - '0' - 1];
}

size_t game::time_left() const {
	ssize_t diff = static_cast<ssize_t>(std::difftime(std::time(nullptr), _start));
	if (diff < 0)
		return 0;
	return static_cast<size_t>(diff);
}

net::action_status game::write_partial_header(std::ostream& out) const {
	if (!out)
		return net::action_status::PERSIST_ERR;
	std::tm* tm = std::gmtime(&_start);
	out << "Game initiated: ";
	out << (tm->tm_year + 1900) << '-' << tm->tm_mon << '-' << tm->tm_mday << ' ';
	out << tm->tm_hour << ':' << tm->tm_min << ':' << tm->tm_sec;
	out << " with " << _duration << "s to be completed." << std::endl;
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

net::action_status game::write_full_header(std::ostream& out) const {
	auto ok = write_partial_header(out);
	if (ok != net::action_status::OK)
		return ok;
	out << "Mode: ";
	if (_debug)
		out << "DEBUG";
	else
		out << "PLAY";
	out << " Secret Code: " << _secret_key << std::endl;
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

net::action_status game::write_trials(std::ostream& out) const {
	if (!out)
		return net::action_status::PERSIST_ERR;
	if (_curr_trial == '0') {
		out << "Game started - no transactions found" << std::endl;
		if (!out)
			return net::action_status::PERSIST_ERR;
		return net::action_status::OK;
	}
	int i = 0;
	for (; i < MAX_TRIALS - '0' - 1; i++) {
		for (int j = 0; j < GUESS_SIZE; j++)
			out << _trials[i].trial[j] << ' ';
		out << static_cast<char>(_trials[i].nB + '0');
		out << static_cast<char>(_trials[i].nW + '0') << '\n';
	}
	if (i < MAX_TRIALS - '0') { // just in case MAX_TRIALS is 0
		for (int j = 0; j < GUESS_SIZE; j++)
			out << _trials[i].trial[j] << ' ';
		out << static_cast<char>(_trials[i].nB + '0');
		out << static_cast<char>(_trials[i].nW + '0');
	}
	out << std::endl;
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

net::action_status game::write_termination(std::ostream& out) const {
	if (!out)
		return net::action_status::PERSIST_ERR;
	auto end_type = has_ended();
	if (end_type == game::result::ONGOING)
		return net::action_status::ONGOING_GAME;
	if (end_type == game::result::LOST_TIME)
		return write_termination(out, "TIMEOUT");
	if (end_type == game::result::LOST_TRIES)
		return write_termination(out, "FAIL");
	return write_termination(out, "WIN");
}

net::action_status game::write_termination(std::ostream& out, const std::string& reason) const {
	if (!out)
		return net::action_status::PERSIST_ERR;
	std::time_t t = std::time(nullptr);
	out << "Termination: " << reason << "at" << t;
	out << ", Duration: " << time_left() << 's' << std::endl;
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

active_games::map_type::iterator active_games::find(const std::string& plid) {
	return _games.find(plid);
}

active_games::map_type::iterator active_games::end() {
	return std::end(_games);
}

net::action_status active_games::erase(const map_type::iterator& it) {
	_games.erase(it);
	return net::action_status::OK;
}
