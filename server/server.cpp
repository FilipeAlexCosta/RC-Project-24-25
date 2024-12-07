#include "game.hpp"

#include <iostream>
#include <ctime>

#define DEFAULT_PORT "58016"

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

static void udp_main(const std::string& port);

static net::action_status start_new_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr,
	active_games& games
);

/*static net::action_status end_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr,
	active_games& games
);

static net::action_status debug_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr,
	active_games& games
);*/

int main() {
	udp_main(DEFAULT_PORT);
	std::cout << "Shutdown complete.\n";
	return 0;
}

void udp_main(const std::string& port) {
	std::srand(std::time(nullptr));
	net::action_map<
		net::udp_source,
		const net::udp_connection&,
		const net::other_address&,
		active_games&
	> actions;
	actions.add_action("SNG", start_new_game);
	/*actions.add_action("QUT", end_game);
	actions.add_action("DBG", debug_game);*/
	net::udp_connection udp_conn{{port, SOCK_DGRAM}};
	active_games games;

	while (!exit_server) {
		net::other_address client_addr;
		auto [status, request] = udp_conn.listen(client_addr);
		status = actions.execute(request, udp_conn, client_addr, games);
		if (status == net::action_status::UNK_ACTION) {
			net::out_stream out;
			out.write("ERR").prime();
			udp_conn.answer(out, client_addr);
			continue;
		}
		if (status != net::action_status::OK)
			std::cerr << net::status_to_message(status) << '.' << std::endl;
	}

	std::cout << "UDP server shutting down...\n";
	return;
}

static net::action_status start_new_game(net::stream<net::udp_source>& req, const net::udp_connection& udp_conn, const net::other_address& client_addr, active_games& games) {
	net::out_stream out_strm;
	out_strm.write("RSG");
	auto [status, fields] = req.read({{4, 6}, {1, 3}});
	if (status != net::action_status::OK || (status = req.no_more_fields()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return net::action_status::OK;
	}
	status = net::is_valid_plid(fields[0]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return net::action_status::OK;
	}
	
	status = net::is_valid_max_playtime(fields[1]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return net::action_status::OK;
	}

	auto gm = games.find(fields[0]);
	if (gm != std::end(games)) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return net::action_status::OK;
	}
	games.emplace(std::move(fields[0]), game{static_cast<uint16_t>(std::stoul(fields[1]))});
	out_strm.write("OK").prime();
	std::cout << out_strm.view();
	udp_conn.answer(out_strm, client_addr);
	return net::action_status::OK;
}

/*static net::action_status end_game(net::stream<net::file_source>& msg, std::string_view host, std::string_view port) {
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
}*/
