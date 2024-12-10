#include "game.hpp"

#include <filesystem>

game::game(uint16_t duration) : _duration{duration}, _debug{false} {
	for (int i = 0; i < GUESS_SIZE; i++)
		_secret_key[i] = net::VALID_COLORS[std::rand() % std::size(net::VALID_COLORS)];
}

game::game(uint16_t duration, const char secret_key[GUESS_SIZE])
	: _duration{duration}, _debug{true} {
	std::copy(secret_key, secret_key + GUESS_SIZE, _secret_key);
}

std::pair<net::action_status, game::result> game::guess(const std::string& valid_plid, char play[GUESS_SIZE]) {
	result res = has_ended();
	if (res != result::ONGOING)
		return {net::action_status::OK, res};
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
	return {net::action_status::OK, has_ended()};
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

game::result game::has_ended() {
	if (_ended != result::ONGOING)
		return _ended;
	if (_curr_trial > '0' && last_trial()->nB == GUESS_SIZE) {
		_ended = result::WON;
	} else if (_curr_trial >= MAX_TRIALS) {
		_ended = result::LOST_TRIES;
	} else if (_start + _duration < std::time(nullptr)) {
		_ended = result::LOST_TIME;
	}
	if (_ended == result::ONGOING)
		return _ended;
	_end = std::time(nullptr);
	if (_end > _start + _duration)
		_end = _start + _duration;
	return _ended;
}

void game::quit() {
	_ended = result::QUIT;
	_end = std::time(nullptr);
	if (_end > _start + _duration)
		_end = _start + _duration;
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
	if (diff < 0 || _ended != result::ONGOING)
		return 0;
	return static_cast<size_t>(diff);
}

size_t game::time_elapsed() const {
	if (_ended != result::ONGOING)
		return std::difftime(_end, _start);
	size_t diff = std::difftime(std::time(nullptr), _start);
	if (diff > _duration)
		return _duration;
	return diff;
}

std::string game::get_active_path(const std::string& valid_plid) {
	return DEFAULT_GAME_DIR + ("/STATE_" + valid_plid + ".txt");
}

std::string game::get_final_path(const std::string& valid_plid) {
	if (_ended == result::ONGOING)
		return "";
	std::tm* tm = std::gmtime(&_end);
	std::stringstream strm;
	strm << std::put_time(tm, "%F_%T");
	if (!strm)
		return "";
	return get_final_dir(valid_plid) + '/' + strm.str() + ".txt";
}

std::string game::get_final_dir(const std::string& valid_plid) {
	return DEFAULT_GAME_DIR + ('/' + valid_plid);
}

int game::setup_directories() {
	try {
		std::filesystem::create_directory(DEFAULT_GAME_DIR);
	} catch (...) {
		return 1;
	}
	return 0;
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
	out << std::put_time(tm, "%F %T") << DEFAULT_SEP;
	out << _start << DEFAULT_EOM << std::flush;
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
	out << std::to_string(time_elapsed()) << DEFAULT_EOM << std::flush;
	// TODO: check for exceptions possibly
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

net::action_status game::write_termination(std::ostream& out) {
	if (!out)
		return net::action_status::PERSIST_ERR;
	has_ended();
	if (_ended == result::ONGOING)
		quit();
	out << static_cast<char>(_ended) << DEFAULT_SEP;
	std::tm* tm = std::gmtime(&_end);
	out << std::put_time(tm, "%F %T") << std::flush;
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
	auto ok = net::action_status::OK;
	try {
		auto act_path = game::get_active_path(it->first);
		std::fstream out{game::get_active_path(it->first), std::ios::out | std::ios::app};
		ok = it->second.write_termination(out);
		if (ok != net::action_status::OK)
			return ok;
		std::string dest_path = it->second.get_final_path(it->first);
		if (dest_path.length() == 0)
			return net::action_status::PERSIST_ERR;
		std::filesystem::create_directory(game::get_final_dir(it->first));
		std::filesystem::rename(act_path, dest_path);
	} catch (std::filesystem::filesystem_error& err) {
		return net::action_status::PERSIST_ERR;
	} catch (std::bad_alloc& err) {
		return net::action_status::PERSIST_ERR; // TODO: this seems worse than PERSIST_ERR
	}
	_games.erase(it);
	return net::action_status::OK;
}
