#ifndef _SCOREBOARD_HPP_
#define _SCOREBOARD_HPP_

/*#include "game.hpp"
#include <array>

#define MAX_TOP_SCORES 10

struct score_board {
	struct score_record {
		uint8_t score;
		uint8_t tries;
		char mode;
		std::array<char, PLID_SIZE> plid;
		std::array<char, GUESS_SIZE> code;
	};

	void add_game(uint8_t score, const char plid[PLID_SIZE], const char code[GUESS_SIZE], uint8_t tries, char mode);
    auto begin() {return scoreboard.begin(); }
    auto end() {return scoreboard.end(); }
private:
    struct GameComparator {
        bool operator()(const game& game_a, const game& game_b) const {
            if (game_a.score() != game_b.score())
                return game_a.score() > game_b.score();
        //  else // TODO: tie-breaker (?)
        //         return std::strcmp(game_a.secret_key(), game_b.secret_key()) < 0;
        
            return false;
        }
    };  
    
    size_t find_game_pos(const game& g);

    std::array<game, MAX_TOP_SCORES> scoreboard;
};
*/
#endif
