#include "game.hpp"

game::game(uint16_t duration) : _duration{duration} {
	for (int i = 0; i < GUESS_SIZE; i++)
		_secret_key[i] = net::VALID_COLORS[std::rand() % std::size(net::VALID_COLORS)];
}

game::game(uint16_t duration, const char secret_key[GUESS_SIZE]) : _duration{duration} {
	std::copy(secret_key, secret_key + GUESS_SIZE, _secret_key);
}

game::result game::guess(char play[GUESS_SIZE]) {
	if (_won)
		return result::WON;
	if (_curr_trial >= MAX_TRIALS)
		return result::LOST_TRIES;
	if (_start + _duration < std::time(nullptr))
		return result::LOST_TIME;
	for (int j = 0; j < GUESS_SIZE; j++)
		_trials[_curr_trial - '0'].trial[j] = play[j];
	_curr_trial++;
	auto [nB, nW] = compare(play);
	if (nB == GUESS_SIZE) {
		_won = true;
		return result::WON;
	}
	return result::ONGOING;
}

std::pair<uint8_t, uint8_t> game::compare(const char guess[GUESS_SIZE]) {
	uint8_t nB = 0, nW = 0;
	for (int guess_it  = 0; guess_it < GUESS_SIZE; guess_it++) {
		if (guess[guess_it] == _secret_key[guess_it]) {
			nB++;
			continue;
		}
		bool done = false;
		for (int real_it = 0; real_it < guess_it; real_it++) {
			if (guess[guess_it] == _secret_key[real_it]) {
				done = true;
				break;
			}
		}
		if (done) {
			nW++;
			continue;
		}
		for (int real_it = guess_it + 1; real_it < GUESS_SIZE; real_it++) {
			if (guess[guess_it] == _secret_key[real_it]) {
				nW++;
				break;
			}
		}
	}
	return {nB, nW};
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
	if (_start + _duration < std::time(nullptr))
		return result::LOST_TIME;
	return result::ONGOING;
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
