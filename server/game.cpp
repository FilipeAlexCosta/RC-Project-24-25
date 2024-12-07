#include "game.hpp"

game::game(uint16_t duration) : _duration{duration} {
	for (int i = 0; i < GUESS_SIZE; i++)
		_secret_key[i] = net::VALID_COLORS[std::rand() % std::size(net::VALID_COLORS)];
}

game::result game::guess(char play[GUESS_SIZE]) {
	if (_won)
		return result::WON;
	if (_curr_trial >= MAX_TRIALS)
		return result::LOST_TRIES;
	if (_start + _duration >= std::time(nullptr))
		return result::LOST_TIME;
	bool equal = true;
	for (int j = 0; j < GUESS_SIZE; j++) {
		_trials[_curr_trial - '0'][j] = play[j];
		equal &= _trials[_curr_trial - '0'][j] == play[j];
	}
	_curr_trial++;
	if (equal) {
		_won = true;
		return result::WON;
	}
	return result::ONGOING;
}

void game::undo_guess() {
	if (_curr_trial == '0')
		return;
	_curr_trial--;
}

game::result game::has_ended() const {
	if (_won)
		return result::WON;
	if (_curr_trial >= MAX_TRIALS)
		return result::LOST_TRIES;
	if (_start + _duration >= std::time(nullptr))
		return result::LOST_TIME;
	return result::ONGOING;
}
