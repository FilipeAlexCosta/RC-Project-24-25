#include "../common/common.hpp"
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
static net::action_status end_game(net::stream<net::file_source>& msg, std::string_view host, std::string_view port);
static net::action_status debug_game(net::stream<net::file_source>& msg, std::string_view host, std::string_view port);

int main() {
	srand(time(0));
	
	net::action_map<net::file_source, std::string_view, std::string_view> actions;
	actions.add_action("SNG", start_new_game);
	actions.add_action("QUT", end_game);
	actions.add_action("DBG", debug_game);

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

// dumb func
static void create_game(int plid, int playtime, std::string key) {
	PLID = plid;
	max_playtime = playtime;
	start_time = std::time(nullptr);
	secret_key = key;
	in_game = true;
}

static net::action_status start_new_game(net::stream<net::file_source>& msg, std::string_view host, std::string_view port) {
	net::out_stream out_strm;
	out_strm.write("RSG");
	auto [status, fields] = msg.read({{4, 6}, {1, 3}});
	if (status != net::action_status::OK || (status = msg.no_more_fields()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return status;
	}
	status = net::is_valid_plid(fields[0]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return status;
	}
	
	status = net::is_valid_max_playtime(fields[1]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return status;
	}
	int plid = std::stoi(fields[0]);
	int playtime = std::stoi(fields[1]);
	if (in_game) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return net::action_status::ONGOING_GAME;
	}

	std::vector<char> color_vector(valid_colors.begin(), valid_colors.end());
	std::string key;
	for (int i = 0; i < 4; i++) {
		if(!key.empty())
			key += ' ';
		key += color_vector[rand() % valid_colors.size()];
	}
	create_game(plid, playtime, key);
	out_strm.write("OK").prime();
	std::cout << out_strm.view();
	
	return net::action_status::OK;
}

static net::action_status end_game(net::stream<net::file_source>& msg, std::string_view host, std::string_view port) {
	net::out_stream out_strm;
	out_strm.write("RQT");
	auto [status, fields] = msg.read({{4, 6}});
	if (status != net::action_status::OK || (status = msg.no_more_fields()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return status;
	}
	status = net::is_valid_plid(fields[0]);

	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return status;
	}
	
	int plid = std::stoi(fields[0]);
	if (!in_game || plid != PLID) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return net::action_status::NOT_IN_GAME;
	}
	in_game = false;
	out_strm.write("OK").write(secret_key).prime();
	std::cout << out_strm.view();
	return net::action_status::OK;
}

static net::action_status debug_game(net::stream<net::file_source>& msg, std::string_view host, std::string_view port) {
	net::out_stream out_strm;
	out_strm.write("RDB");
	auto [status, fields] = msg.read({{4, 6}, {1, 3}, {1, 1}, {1, 1}, {1, 1}, {1, 1}});
	if (status != net::action_status::OK || (status = msg.no_more_fields()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return status;
	}
	
	status = net::is_valid_plid(fields[0]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return status;
	}
	
	status = net::is_valid_max_playtime(fields[1]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return status;
	}

	int plid = std::stoi(fields[0]);
	int playtime = std::stoi(fields[1]);
	
	if(in_game) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return net::action_status::ONGOING_GAME;
	}
	net::out_stream key;
	for (size_t i = 2; i < 2 + GUESS_SIZE; i++) {
		if ((status = net::is_valid_color(fields[i])) != net::action_status::OK) {
			out_strm.write("ERR").prime();
			std::cout << out_strm.view();
			return status;
		}
		key.write(fields[i]);
	}
	out_strm.write("OK").prime();
	std::cout << out_strm.view();
	create_game(plid, playtime, key.view().data());

	return net::action_status::OK;
}
