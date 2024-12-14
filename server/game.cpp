#include "game.hpp"

#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <optional>

game::game(const std::string& valid_plid, uint16_t duration)
	: _plid{valid_plid}, _duration{duration}, _mode{'P'} {
	for (int i = 0; i < GUESS_SIZE; i++)
		_secret_key[i] = net::VALID_COLORS[std::rand() % std::size(net::VALID_COLORS)];
}

game::game(const std::string& valid_plid, uint16_t duration, const char secret_key[GUESS_SIZE])
	: _plid{valid_plid}, _duration{duration}, _mode{'D'} {
	std::copy(secret_key, secret_key + GUESS_SIZE, _secret_key);
}

bool game::is_debug() const {
	return _mode != 'D';
}

std::pair<net::action_status, game::result> game::guess(const std::string& valid_plid, char play[GUESS_SIZE]) {
	auto res = has_ended();
	if (res.second != result::ONGOING)
		return res;
	for (int j = 0; j < GUESS_SIZE; j++)
		_trials[_curr_trial - '0'].trial[j] = play[j];
	auto [nB, nW] = compare(play);
	_trials[_curr_trial - '0'].nB = nB;
	_trials[_curr_trial - '0'].nW = nW;
	_trials[_curr_trial - '0']._when = static_cast<uint16_t>(std::difftime(std::time(nullptr), _start));
	std::fstream out{get_active_path(valid_plid), std::ios::out | std::ios::app};
	if (!out)
		return {net::action_status::FS_ERR, result::ONGOING};
	auto ok = write_trial(_curr_trial - '0', out);
	if (ok != net::action_status::OK)
		return {ok, result::ONGOING};
	_curr_trial++;
	return has_ended();
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

std::pair<net::action_status, game::result> game::has_ended() {
	if (_ended != result::ONGOING)
		return {net::action_status::OK, _ended};
	if (_curr_trial > '0' && last_trial()->nB == GUESS_SIZE) {
		_ended = result::WON;
	} else if (_curr_trial >= MAX_TRIALS) {
		_ended = result::LOST_TRIES;
	} else if (_start + _duration < std::time(nullptr)) {
		_ended = result::LOST_TIME;
	}
	if (_ended == result::ONGOING)
		return {net::action_status::OK, _ended};
	_end = std::time(nullptr);
	if (_end > _start + _duration)
		_end = _start + _duration;
	std::fstream out{get_active_path(_plid), std::ios::out | std::ios::app};
	if (!out) {
		_ended = result::ONGOING;
		return {net::action_status::FS_ERR, _ended};
	}
	return {terminate(out), _ended};
}

net::action_status game::quit() {
	if (_ended == result::QUIT)
		return net::action_status::OK;
	if (_ended != result::ONGOING)
		return net::action_status::NOT_IN_GAME;
	_ended = result::QUIT;
	_end = std::time(nullptr);
	if (_end > _start + _duration)
		_end = _start + _duration;
	std::fstream out{get_active_path(_plid), std::ios::out | std::ios::app};
	if (!out) {
		_ended = result::ONGOING;
		return net::action_status::FS_ERR;
	}
	return terminate(out);
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

int game::score() const {
	if (_ended != result::WON)
		return MIN_SCORE - 1;
	size_t dur = std::difftime(_end, _start);
	float from_tri = (((MAX_TRIALS - '0') - (_curr_trial - '0')) + 1) / static_cast<float>(MAX_TRIALS - '0');
	float from_dur = (MAX_PLAYTIME - dur) / static_cast<float>(MAX_PLAYTIME);
	float res = from_tri * SCORE_TRIAL_WEIGHT + from_dur * SCORE_DURATION_WEIGHT;
	res /= SCORE_TRIAL_WEIGHT + SCORE_DURATION_WEIGHT;
	return static_cast<uint32_t>(res * (MAX_SCORE - MIN_SCORE) + MIN_SCORE);
}

std::string game::get_active_path(const std::string& valid_plid) {
	return DEFAULT_GAME_DIR + ("/STATE_" + valid_plid) + ".txt";
}

std::string game::get_final_path(const std::string& valid_plid) {
	return DEFAULT_GAME_DIR + ('/' + valid_plid);
}

static std::optional<std::string> get_latest_file(const std::string& dirp) {
	std::string path = "";
	try {
		for (const auto& file : std::filesystem::directory_iterator{dirp}) {
			if (file.is_regular_file() &&
				file.path().filename().string() > path)
				path = file.path().filename().string();
		}
	} catch (std::exception& err) {
		return {};
	}
	if (path.empty())
		return {""};
	return dirp + '/' + path;
}

/*std::string game::prepare_trial_file(net::stream<net::file_source>& stream) {
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
}*/

std::pair<net::action_status, game> game::create(const std::string& valid_plid, uint16_t duration) {
	game gm{valid_plid, duration};
	auto status = gm.create();
	return {status, std::move(gm)};
}

std::pair<net::action_status, game> game::create(const std::string& valid_plid, uint16_t duration, const char secret_key[GUESS_SIZE]) {
	game gm{valid_plid, duration, secret_key};
	auto status = gm.create();
	return {status, std::move(gm)};
}

net::action_status game::create() {
	std::string path = DEFAULT_GAME_DIR + ("/STATE_" + _plid) + ".txt";
	try {
		if (std::filesystem::exists(path))
			return net::action_status::ONGOING_GAME;
	} catch (std::exception& err) {
		return net::action_status::FS_ERR;
	}
	std::fstream out{path, std::ios::out};
	if (!out)
		return net::action_status::FS_ERR;
	out << _plid << DEFAULT_SEP << _mode;
	out << DEFAULT_SEP << _secret_key << DEFAULT_SEP << _duration << DEFAULT_SEP;
	out << _start << DEFAULT_EOM << std::flush;
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

std::pair<net::action_status, game> game::find_active(const std::string& valid_plid) {
	std::string path = get_active_path(valid_plid);
	int fd = open(path.c_str(), O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT)
			return {net::action_status::NOT_IN_GAME, {}};
		return {net::action_status::FS_ERR, {}};
	}
	net::stream<net::file_source> in{{fd}};
	auto res = parse(in);
	if (close(fd) == -1 && res.first != net::action_status::OK)
		res.first = net::action_status::FS_ERR;
	auto ended = res.second.has_ended();
	if (ended.first != net::action_status::OK)
		return {ended.first, {}};
	if (ended.second == result::ONGOING)
		return {net::action_status::OK, res.second};
	return {net::action_status::NOT_IN_GAME, {}};
}

std::pair<net::action_status, game> game::find_any(const std::string& valid_plid) {
	auto res = find_active(valid_plid);
	if (res.first != net::action_status::NOT_IN_GAME)
		return res;
	auto path = get_latest_file(get_final_path(valid_plid));
	if (!path.has_value())
		return {net::action_status::FS_ERR, {}};
	int fd = -1;
	if ((fd = open(path.value().c_str(), O_RDONLY)) == -1) {
		if (errno == ENOENT)
			return {net::action_status::NOT_IN_GAME, {}}; // finer status maybe
		return {net::action_status::FS_ERR, {}};
	}
	net::stream<net::file_source> in{{fd}};
	res = parse(in);
	if (close(fd) == -1 && res.first == net::action_status::OK)
		res.first = net::action_status::FS_ERR;
	return res;
}

std::pair<net::action_status, game> game::parse(net::stream<net::file_source>& in) {
	auto r = in.read({
		{PLID_SIZE, PLID_SIZE}, // PLID
		{1, 1}, // MODE
		{GUESS_SIZE, GUESS_SIZE}, // KEY
		{1, MAX_PLAYTIME_SIZE}, // DURATION
		{1, SIZE_MAX}} // START
	);
	if (r.first != net::action_status::OK)
		return {r.first, {}}; // if failed to read header
	game gm{};
	gm._plid = std::move(r.second[0]);
	gm._mode = r.second[1][0];
	std::copy(std::begin(r.second[2]), std::end(r.second[2]), gm._secret_key);
	try {
		gm._duration = std::stoul(r.second[3]);
		gm._start = std::time_t(std::stoul(r.second[4]));
	} catch (std::invalid_argument& err) {
		return {net::action_status::BAD_ARG, {}};
	} catch (std::out_of_range& err) {
		return {net::action_status::BAD_ARG, {}};
	}
	in.reset();
	bool finished = false;
	std::pair<net::action_status, std::string> trial_number;
	for (int i = 0; i < MAX_TRIALS - '0'; i++) {
		trial_number = in.read(1, 1);
		if (trial_number.first == net::action_status::MISSING_EOM)
			break;
		if (trial_number.first != net::action_status::OK)
			return {trial_number.first, {}};
		if (trial_number.second[0] < '1' || trial_number.second[0] > MAX_TRIALS) {
			finished = true;
			break;
		}
		r = in.read({
			{GUESS_SIZE, GUESS_SIZE}, // GUESS
			{1, 1}, // nB
			{1, 1}, // nW
			{1, MAX_PLAYTIME_SIZE} // WHEN
		});
		if (r.first != net::action_status::OK)
			return {r.first, {}};
		std::copy(std::begin(r.second[0]), std::end(r.second[0]), gm._trials[i].trial);
		gm._trials[i].nB = r.second[1][0] - '0';
		gm._trials[i].nW = r.second[2][0] - '0';
		try {
			gm._trials[i]._when = std::stoul(r.second[3]);
		} catch (std::invalid_argument& err) {
			return {net::action_status::BAD_ARG, {}};
		} catch (std::out_of_range& err) {
			return {net::action_status::BAD_ARG, {}};
		}
		gm._curr_trial++;
		in.reset();
	}
	if (!finished)
		return {net::action_status::OK, gm};
	switch (trial_number.second[0]) {
	case static_cast<char>(result::LOST_TRIES):
	case static_cast<char>(result::LOST_TIME):
	case static_cast<char>(result::WON):
	case static_cast<char>(result::QUIT):
		gm._ended = static_cast<result>(trial_number.second[0]);
		break;
	default:
		return {net::action_status::BAD_ARG, {}};
	}
	auto end_time = in.read(1, SIZE_MAX);
	if (end_time.first != net::action_status::OK)
		return {end_time.first, {}};
	try {
		gm._end = std::time_t(std::stoul(end_time.second));
	} catch (std::invalid_argument& err) {
		return {net::action_status::BAD_ARG, {}};
	} catch (std::out_of_range& err) {
		return {net::action_status::BAD_ARG, {}};
	}
	if ((end_time.first = in.no_more_fields()) != net::action_status::OK)
		return {end_time.first, {}};
	return {net::action_status::OK, gm};
}

int game::setup_directories() {
	try {
		std::filesystem::create_directory(DEFAULT_GAME_DIR);
	} catch (...) {
		return 1;
	}
	return 0;
}

net::action_status game::write_trial(uint8_t trial, std::ostream& out) const {
	out << static_cast<char>(trial + '1') << DEFAULT_SEP;
	out << std::string_view{_trials[trial].trial, GUESS_SIZE} << DEFAULT_SEP;
	out << std::to_string(_trials[trial].nB) << DEFAULT_SEP;
	out << std::to_string(_trials[trial].nW) << DEFAULT_SEP;
	out << std::to_string(time_elapsed()) << DEFAULT_EOM << std::flush;
	// TODO: check for exceptions
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

net::action_status game::terminate(std::ostream& out) {
	if (_ended == result::ONGOING)
		return net::action_status::ONGOING_GAME;
	out << static_cast<char>(_ended) << DEFAULT_SEP;
	out << _end << DEFAULT_EOM << std::flush;
	if (!out)
		return net::action_status::PERSIST_ERR;
	try {
		std::stringstream path;
		std::string final_dir = get_final_path(_plid);
		std::filesystem::create_directory(final_dir);
		path << final_dir << '/';
		std::tm* tm = std::gmtime(&_end);
		path << std::put_time(tm, "%F_%T") << ".txt";
		if (!path)
			return net::action_status::PERSIST_ERR;
		std::filesystem::rename(get_active_path(_plid), path.str());
	} catch (std::exception& err) {
		return net::action_status::PERSIST_ERR;
	}
	return net::action_status::OK;
}
