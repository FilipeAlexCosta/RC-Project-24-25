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
	actions.add_action("QUT", end_game);
	actions.add_action("DBG", start_new_game_debug);
	actions.add_action("TRY", do_try);
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
	games.emplace(std::move(fields[0]), game{static_cast<uint16_t>(std::stoul(fields[1]))});
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

	game::result game_res = gm->second.guess(play);
	if (game_res == game::result::LOST_TIME || game_res == game::result::LOST_TRIES) {
		if (game_res == game::result::LOST_TIME)
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
	return udp_conn.answer(out_strm, client_addr);
}
