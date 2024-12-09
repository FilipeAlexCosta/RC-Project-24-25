#ifndef _GAME_HPP_
#define _GAME_HPP_

#include "../common/common.hpp"

#include <ctime>
#include <fstream>

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
	size_t time_left() const;
	net::action_status write_partial_header(std::ostream& out) const;
	net::action_status write_full_header(std::ostream& out) const;
	net::action_status write_trials(std::ostream& out) const;
	net::action_status write_termination(std::ostream& out) const;
	net::action_status write_termination(std::ostream& out, const std::string& reason) const;
private:
	std::pair<uint8_t, uint8_t> compare(const char guess[GUESS_SIZE]);
	uint16_t _duration; // in seconds
	std::time_t _start{std::time(nullptr)};
	bool _debug;
	char _curr_trial{'0'};
	char _secret_key[GUESS_SIZE];
	trial_record _trials[MAX_TRIALS - '0'];
};

//using active_games = std::unordered_map<std::string, game>;

struct active_games {
	using map_type = std::unordered_map<std::string, game>;
	map_type::iterator find(const std::string& plid);
	map_type::iterator end();
	net::action_status erase(const map_type::iterator& it);

	template<typename K, typename V>
	net::action_status emplace(K&& plid, V&& game) {
		std::fstream out{plid, std::ios::out | std::ios::trunc};
		auto [inserted, _] = _games.emplace(std::move(plid), std::move(game));
		auto ok = inserted->second.write_partial_header(out);
		if (ok != net::action_status::OK) {
			_games.erase(inserted);
			return ok;
		}
		if ((ok = inserted->second.write_trials(out)) != net::action_status::OK) {
			_games.erase(inserted);
			return ok;
		}
		return net::action_status::OK;
	}
private:
	map_type _games;
};

#endif
