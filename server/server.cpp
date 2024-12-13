#include "game.hpp"
#include "scoreboard.hpp" // TODO: change

#include <iostream>
#include <ctime>
#include <filesystem>
#include <fcntl.h>

#define DEFAULT_PORT "58016"

static bool exit_server = false;

//static ScoreBoard sb; // TODO: change

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
static void tcp_main(const std::string& port);

static net::action_status start_new_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr,
	active_games& games
);

static net::action_status end_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr,
	active_games& games
);

static net::action_status start_new_game_debug(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr,
	active_games& games
);

static net::action_status do_try(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr,
	active_games& games
);

static net::action_status show_trials(
	net::stream<net::tcp_source>& req,
	const net::tcp_connection& tcp_conn
);

int main() {
	if (game::setup_directories() != 0) {
		std::cout << "Failed to setup the " << DEFAULT_GAME_DIR << " directory.\n";
		std::cout << "Shutting down.\n";
	}
	udp_main(DEFAULT_PORT);
//	tcp_main(DEFAULT_PORT);
	std::cout << "Shutdown complete.\n";
	return 0;
}

static void udp_main(const std::string& port) {
	std::srand(std::time(nullptr));
	net::action_map<
		net::udp_source,
		const net::udp_connection&,
		const net::other_address&,
		active_games&
	> actions;
	actions.add_action("SNG", start_new_game);
	actions.add_action("QUT", end_game);
	actions.add_action("DBG", start_new_game_debug);
	actions.add_action("TRY", do_try);
	net::udp_connection udp_conn{{port, SOCK_DGRAM}};
	if (!udp_conn.valid()) {
		std::cout << "Failed to open udp connection at " << port << '.';
		std::cout << "UDP server shutting down...\n";
		return;
	}
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

static void tcp_main(const std::string& port) {
	net::action_map<
		net::tcp_source,
		const net::tcp_connection&
	> actions;
	actions.add_action("STR", show_trials);
	//actions.add_action("SSB", end_game);
	net::tcp_server tcp_sv{{port, SOCK_STREAM}};
	active_games games;

	while (!exit_server) {
		net::other_address client_addr;
		auto [status, tcp_conn] = tcp_sv.accept_client(client_addr);
		// TODO: fork
		net::stream<net::tcp_source> request = tcp_conn.to_stream();
		status = actions.execute(request, tcp_conn);
		if (status == net::action_status::UNK_ACTION) {
			net::out_stream out;
			out.write("ERR").prime();
			tcp_conn.answer(out);
			continue;
		}
		if (status != net::action_status::OK)
			std::cerr << net::status_to_message(status) << '.' << std::endl;
	}

	std::cout << "UDP server shutting down...\n";
	return;
}

static net::action_status start_new_game(net::stream<net::udp_source>& req,
										 const net::udp_connection& udp_conn,
										 const net::other_address& client_addr,
										 active_games& games) {
	net::out_stream out_strm;
	out_strm.write("RSG");
	auto [status, fields] = req.read({{4, 6}, {1, 3}});
	if (status != net::action_status::OK || (status = req.no_more_fields()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	status = net::is_valid_plid(fields[0]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	
	status = net::is_valid_max_playtime(fields[1]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	auto gm = games.find(fields[0]);
	if (gm != std::end(games)) {
		if (gm->second.has_ended() == game::result::ONGOING) {
			out_strm.write("NOK").prime();
			std::cout << out_strm.view();
			return udp_conn.answer(out_strm, client_addr);	
		}
		games.erase(gm);
	}
	status = games.emplace(std::move(fields[0]), game{static_cast<uint16_t>(std::stoul(fields[1]))});
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime(); // TODO: what to send in case of sv failure?
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);	
		return status;
	}
	out_strm.write("OK").prime();
	std::cout << out_strm.view();
	return udp_conn.answer(out_strm, client_addr);
}

static net::action_status end_game(net::stream<net::udp_source>& req,
								   const net::udp_connection& udp_conn,
								   const net::other_address& client_addr,
								   active_games& games) {
	net::out_stream out_strm;
	out_strm.write("RQT");
	auto [status, plid] = req.read(6, 6);
	if (status != net::action_status::OK
		|| (status = req.no_more_fields()) != net::action_status::OK
		|| (status = net::is_valid_plid(plid)) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	auto gm = games.find(plid);
	if (gm == std::end(games)) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	if (gm->second.has_ended() != game::result::ONGOING) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		games.erase(gm);
		return udp_conn.answer(out_strm, client_addr);
	}

	out_strm.write("OK");
	for (int i = 0; i < GUESS_SIZE; i++)
		out_strm.write(gm->second.secret_key()[i]);
	out_strm.prime();
	games.erase(gm);
	// TODO: persist game
	std::cout << out_strm.view();
	return udp_conn.answer(out_strm, client_addr);
}

static net::action_status start_new_game_debug(net::stream<net::udp_source>& req,
										 const net::udp_connection& udp_conn,
										 const net::other_address& client_addr,
										 active_games& games) {
	net::out_stream out_strm;
	out_strm.write("RDB");
	auto [status, fields] = req.read({{6, 6}, {1, 3}});
	if (status != net::action_status::OK
		|| (status = net::is_valid_plid(fields[0])) != net::action_status::OK
		|| (status = net::is_valid_max_playtime(fields[1])) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	char secret_key[GUESS_SIZE];
	for (int i = 0; i< GUESS_SIZE; i++) {
		auto [stat, col] = req.read(1, 1);
		if (stat != net::action_status::OK
			|| (stat = net::is_valid_color(col)) != net::action_status::OK) {
			status = stat;
			break;
		}
		secret_key[i] = col[0];
	}
	if (status != net::action_status::OK ||
		(status = req.no_more_fields()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	auto gm = games.find(fields[0]);
	if (gm != std::end(games)) {
		if (gm->second.has_ended() == game::result::ONGOING) {
			out_strm.write("NOK").prime();
			std::cout << out_strm.view();
			return udp_conn.answer(out_strm, client_addr);	
		}
		games.erase(gm);
	}

	games.emplace(std::move(fields[0]), game{static_cast<uint16_t>(std::stoul(fields[1])), secret_key});
	out_strm.write("OK").prime();
	std::cout << out_strm.view();
	return udp_conn.answer(out_strm, client_addr);
}

static net::action_status do_try(net::stream<net::udp_source>& req,
										 const net::udp_connection& udp_conn,
										 const net::other_address& client_addr,
										 active_games& games) {
	net::out_stream out_strm;
	out_strm.write("RTR");
	auto res = req.read(6, 6);
	if (res.first != net::action_status::OK
		|| (res.first = net::is_valid_plid(res.second)) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	std::string plid{std::move(res.second)};

	char play[GUESS_SIZE];
	for (int i = 0; i< GUESS_SIZE; i++) {
		res = req.read(1, 1);
		if (res.first != net::action_status::OK
			|| (res.first = net::is_valid_color(res.second)) != net::action_status::OK)
			break;
		play[i] = res.second[0];
	}
	if (res.first != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	char trial;
	res = req.read(1, 1);
	if (res.first != net::action_status::OK
		|| (res.first = req.no_more_fields()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	trial = res.second[0];
	auto gm = games.find(plid);
	if (gm == std::end(games)) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	char duplicate_at = gm->second.is_duplicate(play);
	if (trial != gm->second.current_trial() + 1) {
		if (trial == gm->second.current_trial() && duplicate_at == gm->second.current_trial()) {
			out_strm.write("OK");
			out_strm.write(gm->second.current_trial());
			out_strm.write(gm->second.last_trial()->nB + '0');
			out_strm.write(gm->second.last_trial()->nW + '0').prime();
			std::cout << out_strm.view();
			return udp_conn.answer(out_strm, client_addr);
		}

		out_strm.write("INV").prime();
		std::cout << out_strm.view();
		games.erase(gm); // TODO: persist game?
		return udp_conn.answer(out_strm, client_addr);
	}

	if (duplicate_at != MAX_TRIALS + 1) {
		out_strm.write("DUP").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	std::fstream out{game::get_active_path(plid), std::ios::in | std::ios::app};
	auto [play_status, play_res] = gm->second.guess(plid, play);
	if (play_status != net::action_status::OK) {
		out_strm.write("INV").prime(); // TODO: unsure if it should return INV in this case
		std::cout << out_strm.view();
		games.erase(gm); // TODO: unsure if it's INV
		udp_conn.answer(out_strm, client_addr);
		return play_status; // return first error
	}
	if (play_res == game::result::LOST_TIME || play_res == game::result::LOST_TRIES) {
		if (play_res == game::result::LOST_TIME)
			out_strm.write("ETM");
		else
			out_strm.write("ENT");
		for (int i = 0; i < GUESS_SIZE; i++)
			out_strm.write(gm->second.secret_key()[i]);
		out_strm.prime();
		std::cout << out_strm.view();
		games.erase(gm); // TODO: persist
		return udp_conn.answer(out_strm, client_addr);
	}
	out_strm.write("OK");
	out_strm.write(gm->second.current_trial());
	out_strm.write(gm->second.last_trial()->nB + '0');
	out_strm.write(gm->second.last_trial()->nW + '0').prime();
	std::cout << out_strm.view();

	// Testing sb
	// if (gm->second.has_ended() == game::result::WON) {
	// 	sb.add_game(gm->second);
	// 	sb.print_sb_test();
	// }

	return udp_conn.answer(out_strm, client_addr);
}

static net::action_status show_trials(net::stream<net::tcp_source>& req,
									  const net::tcp_connection& tcp_conn) {
	auto [status, plid] = req.read(PLID_SIZE, PLID_SIZE);
	net::out_stream out_strm;
	out_strm.write("RST");
	if (status != net::action_status::OK ||
		(status = req.no_more_fields()) != net::action_status::OK ||
		(status = net::is_valid_plid(plid)) != net::action_status::OK) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return tcp_conn.answer(out_strm);
	}

	int game_fd = open(game::get_active_path(plid).c_str(), O_RDONLY);
	if (game_fd == -1 && errno != ENOENT) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return tcp_conn.answer(out_strm);
	}

	if (errno != ENOENT) {
		out_strm.write("ACT");
		net::stream<net::file_source> source{{game_fd}};
		std::cout << game::prepare_trial_file(source) << std::endl;
		return net::action_status::OK;
	}

	std::string path;
	try {
		path = game::get_latest_path(plid);
	} catch (...) { // TODO: finer exceptions
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		tcp_conn.answer(out_strm);
		return net::action_status::PERSIST_ERR;
	}

	if (path.empty()) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return tcp_conn.answer(out_strm);
	}
	if ((game_fd = open(path.c_str(), O_RDONLY)) == -1) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		tcp_conn.answer(out_strm);
		return net::action_status::PERSIST_ERR;
	}
	out_strm.write("FIN");
	net::stream<net::file_source> source{{game_fd}};
	std::cout << game::prepare_trial_file(source) << std::endl;
	return net::action_status::OK;
}
