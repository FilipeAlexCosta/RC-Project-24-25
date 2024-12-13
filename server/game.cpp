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

uint32_t game::score() const {
	if (_ended != result::WON)
		return 0;
	size_t dur = std::difftime(_end, _start);
	float from_tri = (((MAX_TRIALS - '0') - (_curr_trial - '0')) + 1) / static_cast<float>(MAX_TRIALS);
	float from_dur = (MAX_PLAYTIME - from_dur) / static_cast<float>(MAX_PLAYTIME);
	float res = from_tri * SCORE_TRIAL_WEIGHT + from_dur * SCORE_DURATION_WEIGHT;
	res /= SCORE_TRIAL_WEIGHT + SCORE_DURATION_WEIGHT;
	return static_cast<uint32_t>(res * (MAX_SCORE - MIN_SCORE) + MIN_SCORE);
}

std::string game::get_fname(const std::string& valid_plid) {
	return "STATE_" + valid_plid + ".txt";
}

std::string game::get_active_path(const std::string& valid_plid) {
	return DEFAULT_GAME_DIR + ('/' + get_fname(valid_plid));
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

std::string game::get_latest_path(const std::string& valid_plid) {
	std::string dirp = get_final_dir(valid_plid);
	std::string path = "";
	for (const auto& file : std::filesystem::directory_iterator{dirp}) {
		if (file.is_regular_file() &&
			file.path().filename().string() > path)
			path = file.path().filename().string();
	}
	return dirp + '/' + path;
}

std::string game::prepare_trial_file(net::stream<net::file_source>& stream) {
	std::string header{};
	auto curr = stream.read(PLID_SIZE, PLID_SIZE); // read PLID
	if (curr.first != net::action_status::OK)
		return "";
	std::string plid = std::move(curr.second);
	curr = stream.read(1, 1);
	if (curr.first != net::action_status::OK)
		return "";
	char game_type = curr.second[0];
	curr = stream.read(GUESS_SIZE, GUESS_SIZE);
	if (curr.first != net::action_status::OK)
		return "";
	std::string secret_key = std::move(curr.second);
	curr = stream.read(1, MAX_PLAYTIME_SIZE);
	if (curr.first != net::action_status::OK)
		return "";
	std::string playtime = std::move(curr.second);
	curr = stream.read(1, SIZE_MAX);
	if (curr.first != net::action_status::OK)
		return "";
	header.append("Game initiated: ");
	header.append(curr.second + ' ');
	curr = stream.read(1, SIZE_MAX);
	if (curr.first != net::action_status::OK)
		return "";
	header.append(curr.second);
	header.append(" with ");
	header.append(playtime);
	header.append(" seconds to be completed.\n");
	curr = stream.read(1, SIZE_MAX);
	if (curr.first != net::action_status::OK && !stream.finished())
		return "";
	std::time_t start_time = std::stoul(curr.second); // TODO: catch exceptions
	std::string trials{};
	bool found_term_reason = false;
	char trial_count = '0';
	while (!stream.finished()) {
		curr = stream.read(1, 1); // trial number or end reason
		if (curr.second[0] < '1' || curr.second[0] > MAX_TRIALS) {
			found_term_reason = true;
			break;
		}
		curr = stream.read(GUESS_SIZE, GUESS_SIZE); // trial
		trials.append("Trial: " + curr.second);
		curr = stream.read(1, 1); // black
		trials.append(", nB: " + curr.second);
		curr = stream.read(1, 1); // white
		trials.append(", nW: " + curr.second);
		curr = stream.read(1, MAX_PLAYTIME_SIZE); // timestamp
		trials.append(" at " + curr.second + "s\n");
		trial_count++;
	}
	if (trial_count == '0') // no trials
		trials.append("Game started - no transactions found\n");
	else
		trials = std::string{"     --- Transactions found: "} + trial_count + " ---\n\n" + trials;
	if (found_term_reason) {
		header.append("Mode: ");
		if (game_type == 'P')
			header.append("PLAY ");
		else
			header.append("DEBUG ");
		header.append(" Secret code: ");
		header.append(secret_key);
		header.append("\n\n");
		trials.append("     Termination: ");
		switch (curr.second[0]) {
			case static_cast<char>(result::WON):
				trials.append("WON ");
				break;
			case static_cast<char>(result::QUIT):
				trials.append("QUIT ");
				break;
			case static_cast<char>(result::LOST_TIME):
				trials.append("TIMEOUT ");
				break;
			case static_cast<char>(result::LOST_TRIES):
				trials.append("FAIL ");
			default:
				return ""; // TODO: check here
		}
		curr = stream.read(1, SIZE_MAX);
		trials.append("at ");
		trials.append(curr.second);
		curr = stream.read(1, SIZE_MAX);
		trials.push_back(' ');
		trials.append(curr.second); // TODO add duration here (may append time size_t and at End of file)
		trials.push_back('\n');
		return header + trials;
	}
	trials.append("\n  -- ");
	trials.append(std::to_string(static_cast<uint16_t>(std::difftime(std::time(nullptr), start_time)))); // TODO: check exceptions
	trials.append(" seconds remaining to be completed --\n");
	return header + trials;
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
	out << _start << std::flush;
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

net::action_status game::write_last_trial(std::ostream& out) const {
	if (!out)
		return net::action_status::PERSIST_ERR;
	if (_curr_trial == '0')
		return net::action_status::OK;
	out << DEFAULT_SEP << _curr_trial << DEFAULT_SEP;
	out << std::string_view{_trials[_curr_trial - '0' - 1].trial, GUESS_SIZE} << DEFAULT_SEP;
	out << std::to_string(_trials[_curr_trial - '0' - 1].nB) << DEFAULT_SEP;
	out << std::to_string(_trials[_curr_trial - '0' - 1].nW) << DEFAULT_SEP;
	out << std::to_string(time_elapsed()) << std::flush;
	// TODO: check for exceptions
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
	out << DEFAULT_SEP << static_cast<char>(_ended) << DEFAULT_SEP;
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
