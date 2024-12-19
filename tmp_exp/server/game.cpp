#include "game.hpp"

#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>

static scoreboard board;

scoreboard::record::record(uint8_t scr, const char id[PLID_SIZE],
	const char key[GUESS_SIZE], char ntries) : tries{ntries}, score{scr} {
	std::copy(id, id + PLID_SIZE, plid);
	std::copy(key, key + GUESS_SIZE, code);
}

size_t scoreboard::find(const record& rec) {
    size_t low = 0;
    size_t high = _records.size();
    while (low < high) { // binary search
        size_t mid = low + (high - low) / 2;
        if (rec.score > _records[mid].score)
            high = mid;
        else
            low = mid + 1;
    }
    return low;    
}

bool scoreboard::add_temp_record(record&& record) {
	size_t at = find(record);
	if (at == MAX_TOP_SCORES) // discard
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
		return materialize(); // write if record was not discarded
}

bool scoreboard::empty() const {
	return _records.empty();
}

std::string scoreboard::to_string() const {
	std::ostringstream out;
	for (const record& rec : _records) {
		out << std::to_string(rec.score) << ' ';
		out << std::string_view{rec.plid, PLID_SIZE} << ' ';
		out << std::string_view{rec.code, GUESS_SIZE} << ' ';
		out << rec.tries << '\n';
	}
	return out.str();
}

const std::string& scoreboard::start_time() const {
	return _start;
}

void scoreboard::materialize() {
	std::fstream out{
		DEFAULT_SCORE_DIR + ('/' + _start),
		std::ios::out | std::ios::trunc
	};
	if (!out)
		throw net::io_error{"Failed to materialize scoreboard"};
	for (const record& rec : _records) {
		out << std::to_string(rec.score) << DEFAULT_SEP;
		out << std::string_view{rec.plid, PLID_SIZE} << DEFAULT_SEP;
		out << std::string_view{rec.code, GUESS_SIZE} << DEFAULT_SEP;
		out << rec.tries << DEFAULT_EOM;
	}
	if (!out)
		throw net::io_error{"Failed to materialize scoreboard"};
}

static std::string get_latest_file(const std::string& dirp) {
	std::string path = "";
	std::time_t path_t;
	try {
		if (!std::filesystem::exists(dirp))
			return "";
		for (const auto& file : std::filesystem::directory_iterator{dirp}) {
			if (!file.is_regular_file())
				continue;
			if (path.empty()) {
				path = file.path().filename().string(); // get filename
				try { // convert filename to a std::time_t
					path_t = std::time_t(std::stoul(path));
				} catch (std::invalid_argument& err) {
					path = "";
				} catch (std::out_of_range& err) {
					path = "";
				}
				continue;
			}
			std::time_t curr_t;
			try {
				curr_t = std::time_t(std::stoul(file.path().filename().string()));
			} catch (std::invalid_argument& err) {
				continue;
			} catch (std::out_of_range& err) {
				continue;
			}
			if (std::difftime(curr_t, path_t) > 0.0) {
				path = file.path().filename().string();
				path_t = curr_t;
			}
		}
	} catch (std::exception& err) {
		throw net::io_error{"Failed to get latest file in dir"};
	}
	return path;
}

scoreboard scoreboard::get_latest(bool keep_name) {
	std::string fname = get_latest_file(DEFAULT_SCORE_DIR);
	if (fname.empty())
		return {}; // no files
	std::string fullpath = DEFAULT_SCORE_DIR + ('/' + fname);
	int fd = open(fullpath.c_str(), O_RDONLY);
	if (fd == -1)
		throw net::io_error{"Failed to open latest scoreboard file"};
	scoreboard sb;
	if (keep_name)
		sb._start = std::move(fname);
	net::stream<net::file_source> in{{fd}};
	while (true) {
		net::message fields;
		uint8_t score = 255;
		try {
			fields = in.read({
				{1, 3}, // score
				{PLID_SIZE, PLID_SIZE}, // plid
				{GUESS_SIZE, GUESS_SIZE}, // code
				{1, 1}, // tries
			});
			score = static_cast<uint8_t>(std::stoul(fields[0]));
		} catch (std::out_of_range& err) {
			throw net::corruption_error{"Corrupted score in scoreboard file"};
		} catch (std::invalid_argument& err) {
			throw net::corruption_error{"Corrupted score in scoreboard file"};
		} catch (net::missing_eom& err) {
			break; // reached the end
		} catch (net::interaction_error& err) {
			close(fd);
			throw net::corruption_error{"Corrupted scoreboard file"};
		}
		sb.add_temp_record({
			score,
			fields[1].c_str(),
			fields[2].c_str(),
			fields[3][0],
		});
		in.reset(); // reset input stream
	}
	if (close(fd) == -1)
		throw net::io_error{"Failed to close scoreboard file"};
	return sb;
}

uint8_t game::score() const {
	return (MAX_TRIALS - _curr_trial + 1) * 100 / (MAX_TRIALS - '0');
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
	_trials[_curr_trial - '0'].when = static_cast<uint16_t>(std::difftime(std::time(nullptr), _start));
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
	} // checks each possible nB

	for (int gue_it = 0; gue_it < GUESS_SIZE; gue_it++) {
		if (gue_mask[gue_it])
			continue; // if has been matched by another color, ignore
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
		return _ended; // return early if the game has already ended
	if (_curr_trial > '0' && last_trial()->nB == GUESS_SIZE) {
		_ended = result::WON;
	} else if (_curr_trial >= MAX_TRIALS) {
		_ended = result::LOST_TRIES;
	} else if (_start + _duration < std::time(nullptr)) {
		_ended = result::LOST_TIME;
	}
	if (_ended == result::ONGOING)
		return _ended; // if it did not end, return
	_end = std::time(nullptr);
	if (_end > _start + _duration) // cap the time
		_end = _start + _duration;
	std::fstream out{get_active_path(_plid), std::ios::out | std::ios::app};
	if (!out) {
		_ended = result::ONGOING;
		throw net::io_error{"Could not write game to disk"};
	}
	terminate(out); // write game to disk
	return _ended;
}

void game::quit() {
	if (_ended == result::QUIT)
		return; // ignore if already quit
	if (_ended != result::ONGOING)
		throw net::game_error{"Tried to quit a finished game"};
	_ended = result::QUIT;
	_end = std::time(nullptr);
	if (_end > _start + _duration) // cap the time just in case
		_end = _start + _duration;
	std::fstream out{get_active_path(_plid), std::ios::out | std::ios::app};
	if (!out) {
		_ended = result::ONGOING;
		throw net::io_error{"Could not write game to disk"};
	}
	return terminate(out); // write game to disk
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

size_t game::time_elapsed() const {
	if (_ended != result::ONGOING)
		return std::difftime(_end, _start);
	size_t diff = std::difftime(std::time(nullptr), _start);
	if (diff > _duration)
		return _duration;
	return diff;
}

std::string game::to_string() const {
	std::ostringstream out;
	for (int i = 0; i < _curr_trial - '0'; i++) {
		for (int j = 0; j < GUESS_SIZE; j++)
			out << _trials[i].trial[j] << ' ';
		out << static_cast<char>(_trials[i].nB + '0') << ' ';
		out << static_cast<char>(_trials[i].nW + '0') << '\n';
	}
	if (_ended == result::ONGOING) { // write seconds left
		out << std::to_string(static_cast<size_t>(std::difftime(_end, _start)));
		out << "s\n";
	}
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
	out << _start << DEFAULT_EOM << std::flush; /// write header to disk
	if (!out)
		throw net::io_error{"Failed to write header to disk"};
}

game game::find_active(const char valid_plid[PLID_SIZE]) {
	std::string path = get_active_path(valid_plid);
	int fd = open(path.c_str(), O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT) // NO ENTRY errno
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
	res.has_ended(); // may end the game
	return res;
}

game game::find_any(const char valid_plid[PLID_SIZE]) {
	game res;
	try {
		return find_active(valid_plid);
	} catch (net::game_error& err) {} // continue if no active games
	auto path = get_final_path(valid_plid);
	path = path + '/' + get_latest_file(path);
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
	for (int i = 0; i < MAX_TRIALS - '0' + 1; i++) {
		try {
			trial_number = in.read(1, 1);
		} catch (net::missing_eom& err) {
			break; // reached end of trials
		} catch (net::corruption_error& err) {
			throw net::corruption_error{"Corrupted game file"};
		}
		if (trial_number[0] < '1' || trial_number[0] > MAX_TRIALS) {
			finished = true; // reached the termination reason
			break;
		}
		if (i == MAX_TRIALS - '0') // too many trials in file
			throw net::corruption_error{"Corrupted game file"};
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
			gm._trials[i].when = std::stoul(r[3]);
		} catch (std::invalid_argument& err) {
			throw net::corruption_error{"Read bad trial time"};
		} catch (std::out_of_range& err) {
			throw net::corruption_error{"Read bad trial time"};
		}
		gm._curr_trial++;
		in.reset(); // reset stream state
	}
	if (!finished) // if the game has not ended
		return gm;
	switch (trial_number[0]) { // check termination reason
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

int setup() {
	try {
		std::filesystem::create_directory(DEFAULT_GAME_DIR);
		std::filesystem::create_directory(DEFAULT_SCORE_DIR);
		board = scoreboard::get_latest(false);
	} catch (std::exception& err){
		std::cout << "Setup error: " << err.what() << '\n';
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
	try { // move to final directory
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
		return; // do not add to scoreboard if not a win
	board.add_record({score(), _plid, _secret_key, _curr_trial});
}
