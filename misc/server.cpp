#include "common.hpp"
#include <iostream>
#include <ctime>

#define PORT "58011"

static bool exit_server = false;

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

static net::action_status start_new_game(const std::string& msg, net::socket_context& udp_info, net::socket_context& tcp_info);


struct Game { 
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
}

int main() {
	srand(time(0));

	net::socket_context udp_info{"tejo.tecnico.ulisboa.pt", PORT, SOCK_DGRAM};
	if (!udp_info.is_valid()) {
		std::cout << "Failed to create udp socket. Check if the provided address and port are correct.\n";
		return 1;
	}

	if (udp_info.set_timeout(UDP_TIMEOUT)) {
		std::cout << "Failed to set udp socket's timeout.\n"; 
		return 1;
	}
	
	net::action_map<net::socket_context&, net::socket_context&> actions;
	actions.add_action("SNG", start_new_game);
	while (!exit_server)  	{
		std::string input;
		std::getline(std::cin, input);
		auto status = actions.execute(input, udp_info, udp_info);
		if (status != net::action_status::OK) {
			std::cerr << net::status_to_message(status) << ".\n";
		}
		 
	}
	

	std::cout << "Exiting the Server application...\n";
	return 0;
}

static net::action_status start_new_game(const std::string& msg, net::socket_context& udp_info, net::socket_context& tcp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {3, PLID_SIZE, -1});
	if (status != net::action_status::OK)
		return status;
	
	status = net::is_valid_plid(fields[1]);
	if (status != net::action_status::OK)
		return status;

	status = net::is_valid_max_playtime(fields[2]);
	if (status != net::action_status::OK)
		return status;

	int plid = std::stoi(std::string(fields[1]));
	int playtime = std::stoi(std::string(fields[2]));
	if (has_ongoing_game(plid)) {
		return net::action_status::ONGOING_GAME;
	}

	std::string key;
	for (int i = 0; i < 4; i++) {
		if(!key.empty())
			key += ' ';
		key += valid_colors[rand() % valid_colors.size()];
	}
	std::cout << plid << "\t" << playtime << "\t" << key <<"\n";
	
	ongoing_games.emplace(plid, Game(plid, playtime, key));

	return net::action_status::OK;
}