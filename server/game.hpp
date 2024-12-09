#ifndef _GAME_HPP_
#define _GAME_HPP_

#include "../common/common.hpp"

#include <ctime>

struct trial_record {
	char trial[GUESS_SIZE];
	uint8_t nB;
	uint8_t nW;
};


struct game {
	enum class result {
		ONGOING,
		WON,
		LOST_TIME,
		LOST_TRIES,
	};

	game(uint16_t duration);
	game(uint16_t duration, const char secret_key[GUESS_SIZE]);
	result guess(char play[GUESS_SIZE]);
	void undo_guess();
	result has_ended() const;
	const char* secret_key() const;
	char current_trial() const;
	const trial_record* last_trial() const;
	char is_duplicate(const char guess[GUESS_SIZE]) const;
private:
	std::pair<uint8_t, uint8_t> compare(const char guess[GUESS_SIZE]);
	uint16_t _duration; // in seconds
	std::time_t _start{std::time(nullptr)};
	bool _won{false};
	char _curr_trial{'0'};
	char _secret_key[GUESS_SIZE];
	trial_record _trials[MAX_TRIALS - '0'];
};

using active_games = std::unordered_map<std::string, game>;

#endif
