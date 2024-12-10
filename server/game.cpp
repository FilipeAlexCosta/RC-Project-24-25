#include "game.hpp"

game::game(uint16_t duration) : _duration{duration}, _debug{false} {
	for (int i = 0; i < GUESS_SIZE; i++)
		_secret_key[i] = net::VALID_COLORS[std::rand() % std::size(net::VALID_COLORS)];
}

game::game(uint16_t duration, const char secret_key[GUESS_SIZE])
	: _duration{duration}, _debug{true} {
	std::copy(secret_key, secret_key + GUESS_SIZE, _secret_key);
}

std::pair<net::action_status, game::result> game::guess(const std::string& valid_plid, char play[GUESS_SIZE]) {
	if (_curr_trial > '0' && last_trial()->nB == GUESS_SIZE)
		return {net::action_status::OK, result::WON};
	if (_curr_trial >= MAX_TRIALS)
		return {net::action_status::OK, result::LOST_TRIES}; // ensures calls after finishing are correct
	if (_start + _duration < std::time(nullptr))
		return {net::action_status::OK, result::LOST_TIME};
	for (int j = 0; j < GUESS_SIZE; j++)
		_trials[_curr_trial - '0'].trial[j] = play[j];
	auto [nB, nW] = compare(play);
	_trials[_curr_trial - '0'].nB = nB;
	_trials[_curr_trial - '0'].nW = nW;
	_curr_trial++;
	std::fstream out{get_active_path(valid_plid), std::ios::out | std::ios::app};
	auto ok = write_last_trial(out);
	if (ok != net::action_status::OK) {
		_curr_trial--;
		return {ok, result::ONGOING};
	}
	if (nB == GUESS_SIZE)
		return {net::action_status::OK, result::WON};
	if (_curr_trial >= MAX_TRIALS) // actual check
		return {net::action_status::OK, result::LOST_TRIES};
	return {net::action_status::OK, result::ONGOING};
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
	ssize_t diff = static_cast<ssize_t>(std::difftime(_start + _duration, std::time(nullptr)));
	if (diff < 0)
		return 0;
	return static_cast<size_t>(diff);
}

size_t game::time_elapsed() const {
	ssize_t diff = static_cast<ssize_t>(std::difftime(std::time(nullptr), _start));
	return static_cast<size_t>(diff);
}

std::string game::get_active_path(const std::string& valid_plid) {
	return "STATE_" + valid_plid + ".txt";
}

net::action_status game::write_header(std::ostream& out, const std::string& valid_plid) const {
	if (!out)
		return net::action_status::PERSIST_ERR;
	out << valid_plid << DEFAULT_SEP;
	if (_debug)
		out << 'D';
	else
		out << 'P';
	out << DEFAULT_SEP << _secret_key << DEFAULT_SEP << _duration << DEFAULT_SEP;
	std::tm* tm = std::gmtime(&_start);
	out << (tm->tm_year + 1900) << '-' << tm->tm_mon << '-' << tm->tm_mday << DEFAULT_SEP;
	out << tm->tm_hour << ':' << tm->tm_min << ':' << tm->tm_sec << DEFAULT_SEP;
	out << _start << DEFAULT_EOM;
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

net::action_status game::write_last_trial(std::ostream& out) const {
	if (!out)
		return net::action_status::PERSIST_ERR;
	if (_curr_trial == '0')
		return net::action_status::OK;
	out << _curr_trial << DEFAULT_SEP;
	out << std::string_view{_trials[_curr_trial - '0' - 1].trial, GUESS_SIZE} << DEFAULT_SEP;
	out << std::to_string(_trials[_curr_trial - '0' - 1].nB) << DEFAULT_SEP;
	out << std::to_string(_trials[_curr_trial - '0' - 1].nW) << DEFAULT_SEP;
	out << std::to_string(time_elapsed()) << DEFAULT_EOM; // check for exceptions possibly
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

net::action_status game::write_termination(std::ostream& out) const {
	if (!out)
		return net::action_status::PERSIST_ERR;
	auto end_type = has_ended();
	char reason = 0;
	switch (end_type) {
		case result::WON: reason = 'W'; break;
		case result::LOST_TIME: reason = 'T'; break;
		case result::LOST_TRIES: reason = 'F'; break;
		case result::ONGOING: reason = 'Q'; // assume QUIT
	}
	out << reason << DEFAULT_SEP;
	std::time_t end_time = std::time(nullptr);
	std::tm* tm = std::gmtime(&end_time);
	out << (tm->tm_year + 1900) << '-' << tm->tm_mon << '-' << tm->tm_mday << DEFAULT_SEP;
	out << tm->tm_hour << ':' << tm->tm_min << ':' << tm->tm_sec << DEFAULT_EOM;
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
	if (it == std::end(_games))
		return net::action_status::OK;
	std::fstream out{game::get_active_path(it->first), std::ios::out | std::ios::app};
	auto ok = it->second.write_termination(out);
	if (ok != net::action_status::OK)
		return ok; // TODO: move to another file
	_games.erase(it);
	return net::action_status::OK;
}
