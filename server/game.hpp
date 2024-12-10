#ifndef _GAME_HPP_
#define _GAME_HPP_

#include "../common/common.hpp"

#include <ctime>
#include <fstream>

#define DEFAULT_GAME_DIR "GAMES"

struct trial_record {
	char trial[GUESS_SIZE];
	uint8_t nB;
	uint8_t nW;
};

struct game {
	enum class result : char {
		ONGOING = 'O',
		WON = 'W',
		LOST_TIME = 'T',
		LOST_TRIES = 'F',
		QUIT = 'Q'
	};

	game(uint16_t duration);
	game(uint16_t duration, const char secret_key[GUESS_SIZE]);
	std::pair<net::action_status, result> guess(const std::string& valid_plid, char play[GUESS_SIZE]);
	result has_ended();
	const char* secret_key() const;
	char current_trial() const;
	const trial_record* last_trial() const;
	char is_duplicate(const char guess[GUESS_SIZE]) const;
	size_t time_left() const;
	size_t time_elapsed() const;
	net::action_status write_header(std::ostream& out, const std::string& valid_plid) const;
	net::action_status write_termination(std::ostream& out);
	std::string get_final_path(const std::string& valid_plid);
	static std::string get_active_path(const std::string& valid_plid);
	static std::string get_final_dir(const std::string& valid_plid);
	static int setup_directories();
private:
	std::pair<uint8_t, uint8_t> compare(const char guess[GUESS_SIZE]);
	net::action_status write_last_trial(std::ostream& out) const;
	void quit();
	uint16_t _duration; // in seconds
	std::time_t _start{std::time(nullptr)};
	std::time_t _end{std::time(nullptr)};
	result _ended{result::ONGOING};
	bool _debug;
	char _curr_trial{'0'};
	char _secret_key[GUESS_SIZE];
	trial_record _trials[MAX_TRIALS - '0'];
};

struct active_games {
	using map_type = std::unordered_map<std::string, game>;
	map_type::iterator find(const std::string& plid);
	map_type::iterator end();
	net::action_status erase(const map_type::iterator& it);

	template<typename K, typename V>
	net::action_status emplace(K&& plid, V&& game) {
		std::fstream out{game::get_active_path(plid), std::ios::out | std::ios::trunc};
		auto ok = game.write_header(out, plid);
		if (ok != net::action_status::OK)
			return ok; // TODO: possibly remove the file
		_games.emplace(std::move(plid), std::move(game));
		return net::action_status::OK;
	}
private:
	map_type _games;
};

#endif
