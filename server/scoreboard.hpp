#ifndef _SCOREBOARD_HPP_
#define _SCOREBOARD_HPP_

#include "game.hpp"
#include <vector>

#define MAX_TOP_SCORES 10

struct score_board {
    void add_game(game& g);
    void print_sb_test();
    bool is_empty() const;
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

    std::vector<game> scoreboard;
};

#endif
