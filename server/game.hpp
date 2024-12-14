#ifndef _GAME_HPP_
#define _GAME_HPP_

#include "../common/common.hpp"

#include <ctime>

#define DEFAULT_GAME_DIR "GAMES"
#define DEFAULT_SCORE_DIR "SCORES"
#define SCORE_TRIAL_WEIGHT 0.5
#define SCORE_DURATION_WEIGHT 0.5
#define MAX_SCORE 100
#define MIN_SCORE 0
#define MAX_TOP_SCORES 10

int setup_directories();

struct scoreboard {
	struct record {
		uint8_t score;
		char tries;
		char mode;
		char plid[PLID_SIZE];
		char code[GUESS_SIZE];
		record(uint8_t scr, const char id[PLID_SIZE], const char key[GUESS_SIZE], char ntries, char gmode);
	};

	net::action_status add_record(record&& record);
	static std::string get_dir();
	static std::pair<net::action_status, scoreboard> get_latest();
private:
    size_t find(const record& g);
	net::action_status materialize();

	std::string _fname{std::string{DEFAULT_SCORE_DIR} + '/' + std::to_string(static_cast<size_t>(std::time(nullptr)))};
	std::vector<record> _records{};
};

struct trial_record {
	char trial[GUESS_SIZE];
	uint8_t nB;
	uint8_t nW;
	uint16_t _when;
};

struct game {
	enum class result : char {
		ONGOING = 'O',
		WON = 'W',
		LOST_TIME = 'T',
		LOST_TRIES = 'F',
		QUIT = 'Q'
	};

	game() = default;
	std::pair<net::action_status, result> guess(char play[GUESS_SIZE]);
	std::pair<net::action_status, result> has_ended();
	net::action_status quit();

	const char* secret_key() const;
	char current_trial() const;
	const trial_record* last_trial() const;
	char is_duplicate(const char guess[GUESS_SIZE]) const;
	size_t time_left() const;
	size_t time_elapsed() const;
	uint8_t score() const;
	std::pair<net::action_status, std::string> to_string() const;

	static std::pair<net::action_status, game> create(const char valid_plid[PLID_SIZE], uint16_t duration);
	static std::pair<net::action_status, game> create(const char valid_plid[PLID_SIZE], uint16_t duration, const char secret_key[GUESS_SIZE]);
	static std::pair<net::action_status, game> find_active(const char valid_plid[PLID_SIZE]);
	static std::pair<net::action_status, game> find_any(const char valid_plid[PLID_SIZE]);
private:
	game(const char valid_plid[PLID_SIZE], uint16_t duration);
	game(const char valid_plid[PLID_SIZE], uint16_t duration, const char secret_key[GUESS_SIZE]);
	net::action_status create();
	static std::string get_active_path(const char valid_plid[PLID_SIZE]);
	static std::string get_final_path(const char valid_plid[PLID_SIZE]);
	static std::pair<net::action_status, game> parse(net::stream<net::file_source>& in);
	std::pair<uint8_t, uint8_t> compare(const char guess[GUESS_SIZE]);
	net::action_status write_trial(uint8_t trial, std::ostream& out) const;
	net::action_status terminate(std::ostream& out);

	uint16_t _duration{601}; // in seconds
	std::time_t _start{std::time(nullptr)};
	std::time_t _end{std::time(nullptr)};
	result _ended{result::ONGOING};
	char _mode{'P'};
	char _curr_trial{'0'};
	char _plid[PLID_SIZE];
	char _secret_key[GUESS_SIZE];
	trial_record _trials[MAX_TRIALS - '0'];
};

#endif
