#ifndef _GAME_HPP_
#define _GAME_HPP_

#include "../common/common.hpp"

#include <ctime>

struct game {
	enum class result {
		ONGOING,
		WON,
		LOST_TIME,
		LOST_TRIES
	};

	game(uint16_t duration);
	result guess(char play[GUESS_SIZE]);
	void undo_guess();
	result has_ended() const;
private:
	uint16_t _duration; // in seconds
	std::time_t _start{std::time(nullptr)};
	bool _won{false};
	char _curr_trial{'0'};
	char _secret_key[GUESS_SIZE];
	char _trials[MAX_TRIALS - '0'][GUESS_SIZE];
};

using active_games = std::unordered_map<std::string, game>;

#endif
