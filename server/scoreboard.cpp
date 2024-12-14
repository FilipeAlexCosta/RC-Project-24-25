#include "scoreboard.hpp"
#include <iostream>

size_t score_board::find_game_pos(const game& g) {
    GameComparator comparator;

    size_t low = 0;
    size_t high = scoreboard.size();

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if(comparator(g, scoreboard[mid]))
            high = mid;
        else
            low = mid + 1;
    }

    return low;    
}

void score_board::add_game(game& g) {
    if (g.has_ended() != game::result::WON)
        return;
    auto pos = find_game_pos(g);
    scoreboard.insert(scoreboard.begin() + pos, g);

    if (scoreboard.size() > MAX_TOP_SCORES)
        scoreboard.pop_back();
}

void score_board::print_sb_test() { // testing TO DELETE LATER
    int rank = 1;
    for (const auto& g : scoreboard) {
        std::cout << rank++ << DEFAULT_SEP << g.current_trial() << DEFAULT_SEP
                  << g.score() << DEFAULT_SEP
                  << "123456" << DEFAULT_SEP;
        for (int i = 0; i < GUESS_SIZE; i++)
			std::cout << g.secret_key()[i];
        std::cout << '\n';
    }
}

bool score_board::is_empty() const {
    return scoreboard.empty();
}