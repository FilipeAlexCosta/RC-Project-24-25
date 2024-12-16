#include "game.hpp"

#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <optional>

#include <iostream>

static scoreboard board;

scoreboard::record::record(uint8_t scr, const char id[PLID_SIZE],
	const char key[GUESS_SIZE], char ntries, char gmode)
	: score{scr}, tries{ntries}, mode{gmode} {
	std::copy(id, id + PLID_SIZE, plid);
	std::copy(key, key + GUESS_SIZE, code);
}

size_t scoreboard::find(const record& rec) {
    size_t low = 0;
    size_t high = _records.size();
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if(rec.score > _records[mid].score)
            high = mid;
        else
            low = mid + 1;
    }
    return low;    
}

bool scoreboard::add_temp_record(record&& record) {
	size_t at = find(record);
	if (at == MAX_TOP_SCORES)
		return false;
	else {
		_records.insert(std::begin(_records) + at, std::move(record));
		if (_records.size() > MAX_TOP_SCORES)
			_records.pop_back();
		return true;
	}
}

net::action_status scoreboard::add_record(record&& record) {
	if (add_temp_record(std::move(record)))
		return materialize();
	return net::action_status::OK;
}

bool scoreboard::empty() const {
	return _records.empty();
}

std::pair<net::action_status, std::string> scoreboard::to_string() const {
	std::stringstream out;
	if (!out)
		return {net::action_status::FS_ERR, {}};
	out << "SCORE\tPLAYER\tCODE\tTRIES\tMODE\n";
	for (const auto& rec : _records) {
		out << std::to_string(rec.score) << '\t';
		out << std::string_view{rec.plid, PLID_SIZE} << '\t';
		out << std::string_view{rec.code, GUESS_SIZE} << '\t';
		out << rec.tries << '\t';
		if (rec.mode == 'P')
			out << "PLAY";
		else
			out << "DEBUG";
		out << '\n';
	}
	if (!out)
		return {net::action_status::PERSIST_ERR, {}};
	return {net::action_status::OK, out.str()};
}

net::action_status scoreboard::materialize() {
	std::fstream out{_fname, std::ios::out | std::ios::trunc};
	if (!out)
		return net::action_status::FS_ERR;
	for (const auto& rec : _records) {
		out << std::to_string(rec.score) << DEFAULT_SEP;
		out << std::string_view{rec.plid, PLID_SIZE} << DEFAULT_SEP;
		out << std::string_view{rec.code, GUESS_SIZE} << DEFAULT_SEP;
		out << rec.tries << DEFAULT_SEP << rec.mode << DEFAULT_EOM;
	}
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

static std::optional<std::string> get_latest_file(const std::string& dirp) {
	std::string path = "";
	std::time_t path_t;
	try {
		for (const auto& file : std::filesystem::directory_iterator{dirp}) {
			if (!file.is_regular_file())
				continue;
			if (path.empty()) {
				path = file.path().filename().string();
				try {
					path_t = std::time_t(std::stoul(path));
				} catch (std::invalid_argument& err) {
					path = "";
					continue;
				}
			}
			std::time_t curr_t;
			try {
				curr_t = std::time_t(std::stoul(file.path().filename().string()));
			} catch (std::invalid_argument& err) {
				continue;
			}
			if (std::difftime(curr_t, path_t) > 0.0) {
				path = file.path().filename().string();
				path_t = curr_t;
			}
		}
	} catch (std::exception& err) {
		return {};
	}
	if (path.empty())
		return {""};
	return dirp + '/' + path;
}

std::pair<net::action_status, scoreboard> scoreboard::get_latest() {
	auto fname = get_latest_file(DEFAULT_SCORE_DIR);
	if (!fname.has_value())
		return {net::action_status::FS_ERR, {}};
	if (fname.value().empty())
		return {net::action_status::OK, {}};
	int fd = open(fname->c_str(), O_RDONLY);
	if (fd == -1)
		return {net::action_status::FS_ERR, {}};
	scoreboard sb;
	net::stream<net::file_source> in{{fd}};
	while (true) {
		auto [stat, fields] = in.read({
			{1, 3}, // score
			{PLID_SIZE, PLID_SIZE}, // plid
			{GUESS_SIZE, GUESS_SIZE}, // code
			{1, 1}, // tries
			{1, 1} // mode
		});
		if (stat == net::action_status::MISSING_EOM)
			break; // TODO: possible error if wrong read instead of end
		if (stat != net::action_status::OK)
			return {stat, {}};
		try {
			sb.add_temp_record({
				static_cast<uint8_t>(std::stoul(fields[0])),
				fields[1].c_str(),
				fields[2].c_str(),
				fields[3][0],
				fields[4][0]
			});
		} catch (std::exception& err) {
			return {net::action_status::BAD_ARG, {}};
		}
		in.reset();
	}
	return {net::action_status::OK, sb};
}

game::game(const char valid_plid[PLID_SIZE], uint16_t duration)
	: _duration{duration}, _mode{'P'} {
	for (int i = 0; i < GUESS_SIZE; i++)
		_secret_key[i] = net::VALID_COLORS[std::rand() % std::size(net::VALID_COLORS)];
	std::copy(valid_plid, valid_plid + PLID_SIZE, _plid);
}

game::game(const char valid_plid[PLID_SIZE], uint16_t duration, const char secret_key[GUESS_SIZE])
	: _duration{duration}, _mode{'D'} {
	std::copy(secret_key, secret_key + GUESS_SIZE, _secret_key);
	std::copy(valid_plid, valid_plid + PLID_SIZE, _plid);
}

std::pair<net::action_status, game::result> game::guess(char play[GUESS_SIZE]) {
	auto res = has_ended();
	if (res.second != result::ONGOING)
		return res;
	for (int j = 0; j < GUESS_SIZE; j++)
		_trials[_curr_trial - '0'].trial[j] = play[j];
	auto [nB, nW] = compare(play);
	_trials[_curr_trial - '0'].nB = nB;
	_trials[_curr_trial - '0'].nW = nW;
	_trials[_curr_trial - '0']._when = static_cast<uint16_t>(std::difftime(std::time(nullptr), _start));
	std::fstream out{get_active_path(_plid), std::ios::out | std::ios::app};
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
	std::array<bool, GUESS_SIZE> sec_mask{false};
	std::array<bool, GUESS_SIZE> gue_mask{false};
	for (int sec_it = 0; sec_it < GUESS_SIZE; sec_it++) {
		if (_secret_key[sec_it] == guess[sec_it]) {
			nB++;
			sec_mask[sec_it] = true;
			gue_mask[sec_it] = true;
		}
	}

	for (int gue_it = 0; gue_it < GUESS_SIZE; gue_it++) {
		if (gue_mask[gue_it])
			continue;
		for (int sec_it = 0; sec_it < GUESS_SIZE; sec_it++) {
			if (!sec_mask[sec_it] && _secret_key[sec_it] == guess[gue_it]) {
				nW++;
				sec_mask[sec_it] = true;
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

uint8_t game::score() const {
	if (_ended != result::WON)
		return 0;
	size_t dur = std::difftime(_end, _start);
	float from_tri = (((MAX_TRIALS - '0') - (_curr_trial - '0')) + 1) / static_cast<float>(MAX_TRIALS - '0');
	float from_dur = (MAX_PLAYTIME - dur) / static_cast<float>(MAX_PLAYTIME);
	float res = from_tri * SCORE_TRIAL_WEIGHT + from_dur * SCORE_DURATION_WEIGHT;
	res /= SCORE_TRIAL_WEIGHT + SCORE_DURATION_WEIGHT;
	return static_cast<uint32_t>(res * (MAX_SCORE - MIN_SCORE) + MIN_SCORE);
}

std::pair<net::action_status, std::string> game::to_string() const {
	std::stringstream out;
	if (!out)
		return {net::action_status::FS_ERR, {}};
	if (_ended == result::ONGOING)
		out << "\tFound an active";
	else
		out << "\tLast finalized game";
	out << " for player " << std::string_view{_plid, PLID_SIZE} << '\n';
	std::tm* tm = std::gmtime(&_end);
	out << "Game initiated: " << std::put_time(tm, "%F %T");
	out << " with " << std::to_string(_duration) << "s to be completed\n";
	if (_ended != result::ONGOING) {
		out << "Mode:";
		if (_mode == 'P')
			out << " PLAY";
		else
			out << " DEBUG";
		out << " Secret code: " << std::string_view{_secret_key, GUESS_SIZE};
	}
	out << '\n';
	if (_curr_trial == '0') {
		out << "\t--- Game started - no transactions found ---";
	} else {
		out << "\t--- Transactions found: ";
		out << _curr_trial << " ---";
	}
	out << '\n';
	for (int i = 0; i < _curr_trial - '0'; i++) {
		out << "Trial: " << std::string_view{_trials[i].trial, GUESS_SIZE};
		out << ", nB: " << static_cast<char>(_trials[i].nB + '0');
		out << ", nW: " << static_cast<char>(_trials[i].nW + '0');
		out << ' ' << std::to_string(_trials[i]._when) << "s\n";
	}
	switch (_ended) {
	case result::ONGOING:
		out << "\t--- " << std::to_string(static_cast<size_t>(time_left()));
		out << " seconds left --- \n";
		if (!out)
			return {net::action_status::PERSIST_ERR, {}};
		return {net::action_status::OK, out.str()};
	case result::LOST_TIME:
		out << "\tTermination: TIMEOUT at ";
		break;
	case result::LOST_TRIES:
		out << "\tTermination: FAIL at ";
		break;
	case result::WON:
		out << "\tTermination: WON at ";
		break;
	case result::QUIT:
		out << "\tTermination: QUIT at ";
	}
	tm = std::gmtime(&_end);
	out << std::put_time(tm, "%F %T") << ", Duration: ";
	out << std::to_string(static_cast<size_t>(std::difftime(_end, _start)));
	out << "s\n";
	if (!out)
		return {net::action_status::PERSIST_ERR, {}};
	return {net::action_status::OK, out.str()};
}

std::string game::get_active_path(const char valid_plid[PLID_SIZE]) {
	return DEFAULT_GAME_DIR + ("/STATE_" + std::string{valid_plid, PLID_SIZE}) + ".txt";
}

std::string game::get_final_path(const char valid_plid[PLID_SIZE]) {
	return DEFAULT_GAME_DIR + ('/' + std::string{valid_plid, PLID_SIZE});
}

std::pair<net::action_status, game> game::create(const char valid_plid[PLID_SIZE], uint16_t duration) {
	game gm{valid_plid, duration};
	auto status = gm.create();
	return {status, std::move(gm)};
}

std::pair<net::action_status, game> game::create(const char valid_plid[PLID_SIZE], uint16_t duration, const char secret_key[GUESS_SIZE]) {
	game gm{valid_plid, duration, secret_key};
	auto status = gm.create();
	return {status, std::move(gm)};
}

net::action_status game::create() {
	std::string path = DEFAULT_GAME_DIR + ("/STATE_" + std::string{_plid, PLID_SIZE}) + ".txt";
	try {
		if (std::filesystem::exists(path))
			return net::action_status::ONGOING_GAME;
	} catch (std::exception& err) {
		return net::action_status::FS_ERR;
	}
	std::fstream out{path, std::ios::out};
	if (!out)
		return net::action_status::FS_ERR;
	out << std::string_view{_plid, PLID_SIZE} << DEFAULT_SEP << _mode;
	out << DEFAULT_SEP << _secret_key << DEFAULT_SEP << _duration << DEFAULT_SEP;
	out << _start << DEFAULT_EOM << std::flush;
	if (!out)
		return net::action_status::PERSIST_ERR;
	return net::action_status::OK;
}

std::pair<net::action_status, game> game::find_active(const char valid_plid[PLID_SIZE]) {
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

std::pair<net::action_status, game> game::find_any(const char valid_plid[PLID_SIZE]) {
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
	std::copy(std::begin(r.second[0]), std::end(r.second[0]), gm._plid);
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

int setup_directories() {
	try {
		std::filesystem::create_directory(DEFAULT_GAME_DIR);
		std::filesystem::create_directory(DEFAULT_SCORE_DIR);
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
		path << static_cast<size_t>(_end);
		if (!path)
			return net::action_status::PERSIST_ERR;
		std::filesystem::rename(get_active_path(_plid), path.str());
	} catch (std::exception& err) {
		return net::action_status::PERSIST_ERR;
	}
	if (_ended != result::WON)
		return net::action_status::OK;
	return board.add_record({
		score(),
		_plid,
		_secret_key,
		_curr_trial,
		_mode
	});
}
