#ifndef _GAME_HPP_
#define _GAME_HPP_

#include "../common/common.hpp"

#include <ctime>

#define DEFAULT_GAME_DIR "GAMES"
#define DEFAULT_SCORE_DIR "SCORES"
#define MAX_TOP_SCORES 10

/// Sets up the game and score directories and intializes the scoreboard.
/// Note that the scoreboard keeps track of the top scores that were played
/// before the current boot up of the server.
/// For instance, if the server initially had n scores stored in its
/// scoreboard and was then shut down, it would remember those n scores
/// the next time it ran, provided the file was correctly saved.
int setup();

/// Stores the top MAX_TOP_SCORES scores of all games, ordered by
/// number of trials needed to win the game.
struct scoreboard {
	/// Represents an entry in the scoreboard.
	struct record {
		char tries;
		char plid[PLID_SIZE];
		char code[GUESS_SIZE];
		record(const char id[PLID_SIZE], const char key[GUESS_SIZE], char ntries);
	};

	/// Adds a record to the scoreboard if it's a new high score;
	/// otherwise just does nothing.
	/// Writes the updated scoreboard to disk.
	/// Throws:
	/// 1. io_error if writing to disk fails.
	void add_record(record&& record);

	/// Returns a visual representation of the scoreaboard,
	/// ready to be sent to the user.
	std::string to_string() const;

	/// Returns true if the scoreboard is empty; false otherwise.
	bool empty() const;

	/// Returns the string format of the time this scoreboard was
	/// created. It also acts essentially as the scoreboard's name
	/// in the filesystem.
	const std::string& start_time() const;

	/// Returns the latest scoreboard in the scores directory.
	/// If keep_name is enabled, the 'start_time' of the current
	/// scoreboard will be the same as the one read in from disk.
	/// Otherwise, it's initialized to the current time.
	static scoreboard get_latest(bool keep_name = true);
private:
	/// Finds where record 'g' should go relative to all other records
	/// in the scoreboard.
    size_t find(const record& g);

	/// Adds a record to the scoreboard (if it's a new high score),
	/// but does not write the updated scoreaboard to disk.
	bool add_temp_record(record&& record);

	/// Writes the scoreboard to disk.
	/// Throws:
	/// 1. io_error if writing to disk fails.
	void materialize();

	std::string _start{std::to_string(static_cast<size_t>(std::time(nullptr)))};
	std::vector<record> _records{};
};

/// Represents a guess.
struct trial_record {
	char trial[GUESS_SIZE];
	uint8_t nB;
	uint8_t nW;
	uint16_t when;
};

/// Represents a Mastermind game.
struct game {
	enum class result : char {
		ONGOING = 'O',
		WON = 'W',
		LOST_TIME = 'T',
		LOST_TRIES = 'F',
		QUIT = 'Q'
	};

	game() = default;

	/// Guesses a new play in the game (does not check for duplicates).
	/// Returns the state of the game after the play.
	/// Writes the game to disk if it ends.
	result guess(char play[GUESS_SIZE]);

	/// Returns the state of the game.
	/// Writes the game to disk if it ends on the method call (not if
	/// the game ended due to some other reason).
	result has_ended();

	/// Quits the game and writes it to disk (if the game wasn't already
	/// over before).
	void quit();

	/// Returns the code the user must guess.
	const char* secret_key() const;

	/// Returns the current trial number (in a char format, do - '0'
	/// to convert to int).
	char current_trial() const;

	/// Returns a pointer to the last trial played, null in the case
	/// no guesses have yet been made.
	const trial_record* last_trial() const;

	/// Returns MAX_TRIAL + 1 if the guess is not a duplicate;
	/// otherwise returns the trial of that guess.
	char is_duplicate(const char guess[GUESS_SIZE]) const;

	/// Returns the time elapsed since the game began.
	size_t time_elapsed() const;

	/// Returns the game in a human readable format, ready to be
	/// sent to the final user.
	std::string to_string() const;

	/// Creates a brand new game.
	/// Writes the game to disk.
	static game create(const char valid_plid[PLID_SIZE], uint16_t duration);

	/// Creates a brand new game in debug mode.
	/// Writes the game to disk.
	static game create(const char valid_plid[PLID_SIZE], uint16_t duration, const char secret_key[GUESS_SIZE]);

	/// Finds the active game for the given plid (if it exists).
	static game find_active(const char valid_plid[PLID_SIZE]);

	/// Finds the latest recorded game (active or not) for the given
	/// plid (provided it exists).
	static game find_any(const char valid_plid[PLID_SIZE]);
private:
	game(const char valid_plid[PLID_SIZE], uint16_t duration);
	game(const char valid_plid[PLID_SIZE], uint16_t duration, const char secret_key[GUESS_SIZE]);
	void create();

	/// Gets the path for the active game of the given plid.
	static std::string get_active_path(const char valid_plid[PLID_SIZE]);

	/// Gets the path for the finished game directory of the given plid.
	static std::string get_final_path(const char valid_plid[PLID_SIZE]);

	/// Parses a game from disk.
	static game parse(net::stream<net::file_source>& in);

	/// Compares a guess with the secret key and returns {nB, nW}.
	std::pair<uint8_t, uint8_t> compare(const char guess[GUESS_SIZE]);

	/// Writes a single trial to disk.
	void write_trial(uint8_t trial, std::ostream& out) const;

	/// Terminates the game (writes the termination reason to disk
	/// and moves it to the finished games directory of the associated
	/// plid).
	void terminate(std::ostream& out);

	uint16_t _duration{601}; // in seconds
	std::time_t _start{std::time(nullptr)};
	std::time_t _end{std::time(nullptr)};
	result _ended{result::ONGOING};
	char _mode{'P'};
	char _curr_trial{'0'};
	char _plid[PLID_SIZE];
	char _secret_key[GUESS_SIZE];
	trial_record _trials[MAX_TRIALS - '0'];
};

#endif
