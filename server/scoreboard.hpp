#ifndef _SCOREBOARD_HPP_
#define _SCOREBOARD_HPP_

#include "game.hpp"
#include <set>

#define MAX_TOP_SCORES 10

struct ScoreBoard {
    void add_game(const game& g);
    void print_sb_test();
private:
    struct GameComparator {
        bool operator()(const game& game_a, const game& game_b) const {
            if (game_a.has_ended() != game::result::WON ||
                game_b.has_ended() != game::result::WON)
                return false;
        
            if (game_a.current_trial() != game_b.current_trial())
                return game_a.current_trial() < game_b.current_trial();
            else
                return std::strcmp(game_a.secret_key(), game_b.secret_key()) < 0;
        }
    };
    
    std::set<game, GameComparator> scoreboard;
};

#endif