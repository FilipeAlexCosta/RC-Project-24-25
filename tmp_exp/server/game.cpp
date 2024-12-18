#include "game.hpp"

#include <filesystem>
#include <fcntl.h>
#include <fstream>
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

void scoreboard::add_record(record&& record) {
	if (add_temp_record(std::move(record)))
		return materialize();
}

bool scoreboard::empty() const {
	return _records.empty();
}

std::string scoreboard::to_string() const {
	std::ostringstream out;
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
	return out.str();
}

const std::string& scoreboard::start_time() const {
	return _start;
}

void scoreboard::materialize() {
	std::fstream out{
		std::string{DEFAULT_SCORE_DIR} + '/' + std::to_string(static_cast<size_t>(std::time(nullptr))),
		std::ios::out | std::ios::trunc
	};
	if (!out)
		throw net::io_error{"Failed to materialize scoreboard"};
	for (const auto& rec : _records) {
		out << std::to_string(rec.score) << DEFAULT_SEP;
		out << std::string_view{rec.plid, PLID_SIZE} << DEFAULT_SEP;
		out << std::string_view{rec.code, GUESS_SIZE} << DEFAULT_SEP;
		out << rec.tries << DEFAULT_SEP << rec.mode << DEFAULT_EOM;
	}
	if (!out)
		throw net::io_error{"Failed to materialize scoreboard"};
}

static std::string get_latest_file(const std::string& dirp) {
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
		throw net::io_error{"Failed to get latest scoreboard file"};
	}
	if (path.empty())
		return "";
	return dirp + '/' + path;
}

scoreboard scoreboard::get_latest() {
	auto fname = get_latest_file(DEFAULT_SCORE_DIR);
	if (fname.empty())
		return {};
	int fd = open(fname.c_str(), O_RDONLY);
	if (fd == -1)
		throw net::io_error{"Failed to open latest scoreboard file"};
	scoreboard sb;
	net::stream<net::file_source> in{{fd}};
	while (true) {
		net::message fields;
		try {
			fields = in.read({
				{1, 3}, // score
				{PLID_SIZE, PLID_SIZE}, // plid
				{GUESS_SIZE, GUESS_SIZE}, // code
				{1, 1}, // tries
				{1, 1} // mode
			}); // TODO: TEST THIS!
		} catch (net::missing_eom& err) {
			break; // reached the end
		} catch (net::interaction_error& err) {
			throw net::corruption_error{"Corrupted scoreboard file"};
		}
		sb.add_temp_record({
			static_cast<uint8_t>(std::stoul(fields[0])),
			fields[1].c_str(),
			fields[2].c_str(),
			fields[3][0],
			fields[4][0]
		});
		in.reset();
	}
	return sb;
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

game::result game::guess(char play[GUESS_SIZE]) {
	auto res = has_ended();
	if (res != result::ONGOING)
		return res;
	for (int j = 0; j < GUESS_SIZE; j++)
		_trials[_curr_trial - '0'].trial[j] = play[j];
	auto [nB, nW] = compare(play);
	_trials[_curr_trial - '0'].nB = nB;
	_trials[_curr_trial - '0'].nW = nW;
	_trials[_curr_trial - '0']._when = static_cast<uint16_t>(std::difftime(std::time(nullptr), _start));
	std::fstream out{get_active_path(_plid), std::ios::out | std::ios::app};
	if (!out)
		throw net::io_error{"Could not write guess to disk"};
	write_trial(_curr_trial - '0', out);
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

game::result game::has_ended() {
	if (_ended != result::ONGOING)
		return _ended;
	if (_curr_trial > '0' && last_trial()->nB == GUESS_SIZE) {
		_ended = result::WON;
	} else if (_curr_trial > MAX_TRIALS) {
		_ended = result::LOST_TRIES;
	} else if (_start + _duration < std::time(nullptr)) {
		_ended = result::LOST_TIME;
	}
	if (_ended == result::ONGOING)
		return _ended;
	_end = std::time(nullptr);
	if (_end > _start + _duration)
		_end = _start + _duration;
	std::fstream out{get_active_path(_plid), std::ios::out | std::ios::app};
	if (!out) {
		_ended = result::ONGOING;
		throw net::io_error{"Could not write game to disk"};
	}
	terminate(out);
	return _ended;
}

void game::quit() {
	if (_ended == result::QUIT)
		return;
	if (_ended != result::ONGOING)
		throw net::game_error{"Tried to quit a finished game"};
	_ended = result::QUIT;
	_end = std::time(nullptr);
	if (_end > _start + _duration)
		_end = _start + _duration;
	std::fstream out{get_active_path(_plid), std::ios::out | std::ios::app};
	if (!out) {
		_ended = result::ONGOING;
		throw net::io_error{"Couldn not write game to disk"};
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

std::string game::to_string() const {
	std::ostringstream out;
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
		return out.str();
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
	return out.str();
}

std::string game::get_active_path(const char valid_plid[PLID_SIZE]) {
	return DEFAULT_GAME_DIR + ("/STATE_" + std::string{valid_plid, PLID_SIZE}) + ".txt";
}

std::string game::get_final_path(const char valid_plid[PLID_SIZE]) {
	return DEFAULT_GAME_DIR + ('/' + std::string{valid_plid, PLID_SIZE});
}

game game::create(const char valid_plid[PLID_SIZE], uint16_t duration) {
	game gm{valid_plid, duration};
	gm.create();
	return gm;
}

game game::create(const char valid_plid[PLID_SIZE], uint16_t duration, const char secret_key[GUESS_SIZE]) {
	game gm{valid_plid, duration, secret_key};
	gm.create();
	return gm;
}

void game::create() {
	std::string path = DEFAULT_GAME_DIR + ("/STATE_" + std::string{_plid, PLID_SIZE}) + ".txt";
	bool exists = false;
	try {
		exists = std::filesystem::exists(path);
	} catch (std::exception& err) {
		throw net::io_error{"Failed while checking if game existed"};
	}
	if (exists)
		throw net::game_error{"Ongoing game"};
	std::fstream out{path, std::ios::out};
	if (!out)
		throw net::io_error{"Failed to open game file"};
	out << std::string_view{_plid, PLID_SIZE} << DEFAULT_SEP << _mode;
	out << DEFAULT_SEP << std::string_view{_secret_key, GUESS_SIZE} << DEFAULT_SEP << _duration << DEFAULT_SEP;
	out << _start << DEFAULT_EOM << std::flush;
	if (!out)
		throw net::io_error{"Failed to write header to disk"};
}

game game::find_active(const char valid_plid[PLID_SIZE]) {
	std::string path = get_active_path(valid_plid);
	int fd = open(path.c_str(), O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT)
			throw net::game_error{"No active games"};
		throw net::io_error{"Failed to open game file"};
	}
	net::stream<net::file_source> in{{fd}};
	game res;
	try {
		res = parse(in);
	} catch (std::runtime_error& err) {
		close(fd);
		throw err;
	}
	if (close(fd) == -1)
		throw net::io_error{"Failed to close game file"};
	auto ended = res.has_ended();
	if (ended == result::ONGOING)
		return res;
	throw net::game_error{"No active games"};
}

game game::find_any(const char valid_plid[PLID_SIZE]) {
	game res;
	try {
		res = std::move(find_active(valid_plid));
	} catch (net::game_error& err) {} // if no active games
	auto path = get_latest_file(get_final_path(valid_plid));
	if (path.empty())
		throw net::game_error{"No recorded games"};
	int fd = -1;
	if ((fd = open(path.c_str(), O_RDONLY)) == -1) {
		if (errno == ENOENT)
			throw net::game_error{"No recorded games"};
		throw net::io_error{"Failed to open game file"};
	}
	net::stream<net::file_source> in{{fd}};
	try {
		res = parse(in);
	} catch (std::runtime_error& err) {
		close(fd);
		throw err;
	}
	if (close(fd) == -1)
		throw net::io_error{"Failed to close game file"};
	return res;
}

game game::parse(net::stream<net::file_source>& in) {
	net::message r;
	try {
		r = in.read({
			{PLID_SIZE, PLID_SIZE}, // PLID
			{1, 1}, // MODE
			{GUESS_SIZE, GUESS_SIZE}, // KEY
			{1, MAX_PLAYTIME_SIZE}, // DURATION
			{1, SIZE_MAX}} // START
		);
	} catch (net::interaction_error& err) {
		throw net::corruption_error{"Corrupted game file"};
	}
	game gm{};
	std::copy(std::begin(r[0]), std::end(r[0]), gm._plid);
	gm._mode = r[1][0];
	std::copy(std::begin(r[2]), std::end(r[2]), gm._secret_key);
	try {
		gm._duration = std::stoul(r[3]);
		gm._start = std::time_t(std::stoul(r[4]));
	} catch (std::invalid_argument& err) {
		throw net::corruption_error{"Read bad game duration/start time"};
	} catch (std::out_of_range& err) {
		throw net::corruption_error{"Read bad game duration/start time"};
	}
	in.reset();
	bool finished = false;
	std::string trial_number;
	for (int i = 0; i < MAX_TRIALS - '0'; i++) {
		try {
			trial_number = in.read(1, 1);
		} catch (net::missing_eom& err) {
			break; // reached end of trials
		} catch (net::corruption_error& err) {
			throw net::corruption_error{"Corrupted game file"};
		}
		if (trial_number[0] < '1' || trial_number[0] > MAX_TRIALS) {
			finished = true;
			break;
		}
		try {
			r = in.read({
				{GUESS_SIZE, GUESS_SIZE}, // GUESS
				{1, 1}, // nB
				{1, 1}, // nW
				{1, MAX_PLAYTIME_SIZE} // WHEN
			});
		} catch (net::corruption_error& err) {
			throw net::corruption_error{"Corrupted game file"};
		}
		std::copy(std::begin(r[0]), std::end(r[0]), gm._trials[i].trial);
		gm._trials[i].nB = r[1][0] - '0';
		gm._trials[i].nW = r[2][0] - '0';
		try {
			gm._trials[i]._when = std::stoul(r[3]);
		} catch (std::invalid_argument& err) {
			throw net::corruption_error{"Read bad trial time"};
		} catch (std::out_of_range& err) {
			throw net::corruption_error{"Read bad trial time"};
		}
		gm._curr_trial++;
		in.reset();
	}
	if (!finished)
		return gm;
	switch (trial_number[0]) {
	case static_cast<char>(result::LOST_TRIES):
	case static_cast<char>(result::LOST_TIME):
	case static_cast<char>(result::WON):
	case static_cast<char>(result::QUIT):
		gm._ended = static_cast<result>(trial_number[0]);
		break;
	default:
		throw net::corruption_error{"Read bad termination reason"};
	}
	net::field end_time;
	try {
		end_time = in.read(1, SIZE_MAX);
		gm._end = std::time_t(std::stoul(end_time));
	} catch (std::invalid_argument& err) {
		throw net::corruption_error{"Read bad end time"};
	} catch (std::out_of_range& err) {
		throw net::corruption_error{"Read bad end time"};
	} catch (net::interaction_error& err) {
		throw net::corruption_error{"Read bad end time"};
	}
	if (!in.no_more_fields())
		throw net::corruption_error{"More fields than expected in game file"};
	return gm;
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

void game::write_trial(uint8_t trial, std::ostream& out) const {
	out << static_cast<char>(trial + '1') << DEFAULT_SEP;
	out << std::string_view{_trials[trial].trial, GUESS_SIZE} << DEFAULT_SEP;
	out << std::to_string(_trials[trial].nB) << DEFAULT_SEP;
	out << std::to_string(_trials[trial].nW) << DEFAULT_SEP;
	out << std::to_string(time_elapsed()) << DEFAULT_EOM << std::flush;
	if (!out)
		throw net::io_error{"Failed to write trial"};
}

void game::terminate(std::ostream& out) {
	if (_ended == result::ONGOING)
		throw net::game_error{"Tried to ilegally terminate an ongoing game"};
	out << static_cast<char>(_ended) << DEFAULT_SEP;
	out << _end << DEFAULT_EOM << std::flush;
	if (!out)
		throw net::io_error{"Failed to write termination reason"};
	bool failed = false;
	try {
		std::stringstream path;
		std::string final_dir = get_final_path(_plid);
		std::filesystem::create_directory(final_dir);
		path << final_dir << '/';
		path << static_cast<size_t>(_end);
		if (!path)
			failed = true;
		else
			std::filesystem::rename(get_active_path(_plid), path.str());
	} catch (std::exception& err) {
		throw net::io_error{"Failed to terminate game in disk"};
	}
	if (failed)
		throw net::io_error{"Failed to write end time"};
	if (_ended != result::WON)
		return;
	board.add_record({
		score(),
		_plid,
		_secret_key,
		_curr_trial,
		_mode
	});
}
