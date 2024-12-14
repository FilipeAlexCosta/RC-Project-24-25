#ifndef _GAME_HPP_
#define _GAME_HPP_

#include "../common/common.hpp"

#include <ctime>

#define DEFAULT_GAME_DIR "GAMES"
#define SCORE_TRIAL_WEIGHT 0.5
#define SCORE_DURATION_WEIGHT 0.5
#define MAX_SCORE 100
#define MIN_SCORE 0

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
	std::pair<net::action_status, result> guess(const std::string& valid_plid, char play[GUESS_SIZE]);
	std::pair<net::action_status, result> has_ended();
	net::action_status quit();

	const char* secret_key() const;
	char current_trial() const;
	const trial_record* last_trial() const;
	char is_duplicate(const char guess[GUESS_SIZE]) const;
	size_t time_left() const;
	size_t time_elapsed() const;
	int score() const;
	bool is_debug() const;

	static std::pair<net::action_status, game> create(const std::string& valid_plid, uint16_t duration);
	static std::pair<net::action_status, game> create(const std::string& valid_plid, uint16_t duration, const char secret_key[GUESS_SIZE]);
	static std::pair<net::action_status, game> find_active(const std::string& valid_plid);
	static std::pair<net::action_status, game> find_any(const std::string& valid_plid);
	static int setup_directories();
private:
	game(const std::string& valid_plid, uint16_t duration);
	game(const std::string& valid_plid, uint16_t duration, const char secret_key[GUESS_SIZE]);
	net::action_status create();
	static std::string get_active_path(const std::string& valid_plid);
	static std::string get_final_path(const std::string& valid_plid);
	static std::pair<net::action_status, game> parse(net::stream<net::file_source>& in);
	std::pair<uint8_t, uint8_t> compare(const char guess[GUESS_SIZE]);
	net::action_status write_trial(uint8_t trial, std::ostream& out) const;
	net::action_status terminate(std::ostream& out);

	std::string _plid{""};
	uint16_t _duration{601}; // in seconds
	std::time_t _start{std::time(nullptr)};
	std::time_t _end{std::time(nullptr)};
	result _ended{result::ONGOING};
	char _mode{'P'};
	char _curr_trial{'0'};
	char _secret_key[GUESS_SIZE];
	trial_record _trials[MAX_TRIALS - '0'];
};

#endif
