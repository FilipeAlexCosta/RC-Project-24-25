#include "common.hpp"
#include <iostream>
#include <ctime>
#include <fstream>

#define PORT "58011"

static bool exit_server = false;

/* Current player data */
static bool in_game = false;
static int PLID;
static int max_playtime;
static std::time_t start_time;
static char current_trial = '0';
static std::string secret_key;

/* signal(SIGPIPE, SIG_IGN)
 * signal(SIGCHILD, SIG_IGN) ignorar estes 2 sinais
 *
 * select() -> bloqueia na chamada enquanto espera por msgs udp/tcp
 * setsockopt() -> coloca um temporizador na socket (util para UDP)
 *
 * while (n < MAX_RESEND) {
 * 	int ret = recvfrom(...)
 * 	if (ret < 0)
 * 		if (errno == EWOULDBLOCK || errno == EAGAIN)
 * 			timeout => Resend message
 * }
 */

/*struct Game { 
	int player_plid;
	int playtime;
	std::string secret_key;
	std::time_t start_time;
	int trial_num;

	Game(int plid, int playtime, const std::string key)
	 : player_plid(plid), playtime(playtime), secret_key(key),  start_time(std::time(nullptr)), trial_num(0) {};

	bool hasEnded() const {
		auto curr_time = std::time(nullptr);
		auto played_time = std::difftime(curr_time, start_time);
		return played_time >= playtime || trial_num > MAX_TRIALS;
	};
};

static std::unordered_map<int, Game> ongoing_games;

static bool has_ongoing_game(int plid) { // TODO: incorporate this in template ?
	return ongoing_games.find(plid) != ongoing_games.end();
}*/

static net::action_status start_new_game(net::stream<net::file_source>& msg, std::string_view host, std::string_view port);

int main() {
	srand(time(0));
	
	net::action_map<net::file_source, std::string_view, std::string_view> actions;
	actions.add_action("SNG", start_new_game);
	while (!exit_server)  	{
		net::stream<net::file_source> strm{STDIN_FILENO, false};
		auto status = actions.execute(strm, "", PORT);
		if (status != net::action_status::OK) {
			std::cerr << net::status_to_message(status) << ".\n";
		}
	}

	std::cout << "Exiting the Server application...\n";
	return 0;
}

static net::action_status start_new_game(net::stream<net::file_source>& msg, std::string_view host, std::string_view port) {
	auto [status, fields] = msg.read({{4, 6}, {1, 3}});
	if (status != net::action_status::OK || (status = msg.no_more_fields()) != net::action_status::OK)
		return status;
	status = net::is_valid_plid(fields[0]);
	if (status != net::action_status::OK)
		return status;
	
	status = net::is_valid_max_playtime(fields[1]);
	if (status != net::action_status::OK)
		return status;
	int plid = std::stoi(fields[0]);
	int playtime = std::stoi(fields[1]);
	if (in_game)
		return net::action_status::ONGOING_GAME;

	std::vector<char> color_vector(valid_colors.begin(), valid_colors.end());
	std::string key;
	for (int i = 0; i < 4; i++) {
		if(!key.empty())
			key += ' ';
		key += color_vector[rand() % valid_colors.size()];
	}
	std::cout << plid << "\t" << playtime << "\t" << key <<"\n";
	
	PLID = plid;
	max_playtime = playtime;
	start_time = std::time(nullptr);
	secret_key = key;
	in_game = true;
	return net::action_status::OK;
}