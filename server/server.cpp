#include "game.hpp"

#include <iostream>
#include <ctime>
#include <fcntl.h>
#include <sys/wait.h>

#define DEFAULT_PORT "58016"

static bool exit_server = false;

struct verbose {
	template<typename... Args>
	static void write(const net::other_address& client, const std::string_view& resp, Args&&... args) {
		if (!_mode)
			return;
		char ip[INET_ADDRSTRLEN];
		const char* res_ip = inet_ntop(AF_INET, &client.addr.sin_addr, ip, sizeof(ip));
		if (!res_ip)
			return; // TODO: print error?
		int port = ntohs(client.addr.sin_port);
		std::cout << "[IP: " << res_ip << ", PORT: " << port << "]\n | Request: ";
		if (sizeof...(Args) != 0)
			impl_verbose(std::forward<Args>(args)...);
		std::cout << "\n | Response: " << resp << '\n' << std::endl;
	}

	static void set(bool mode) { _mode = mode; }
private:
	template<typename Arg>
	static void impl_verbose(Arg&& arg) {
		std::cout << std::forward<Arg>(arg);
	}

	template<typename Arg, typename... Args>
	static void impl_verbose(Arg&& arg, Args&&... args) {
		std::cout << std::forward<Arg>(arg);
		impl_verbose(std::forward<Args>(args)...);
	}
	static bool _mode;
};

bool verbose::_mode{false};

using udp_action_map = net::action_map<
	net::udp_source,
	const net::udp_connection&,
	const net::other_address&
>;

using tcp_action_map = net::action_map<
	net::tcp_source,
	const net::tcp_connection&
>;

static net::action_status handle_udp(net::udp_connection& udp_conn, const udp_action_map& actions);
static net::action_status handle_tcp(net::tcp_server& tcp_sv, const tcp_action_map& actions);

static net::action_status start_new_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static net::action_status end_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static net::action_status start_new_game_debug(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static net::action_status do_try(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static net::action_status show_trials(
	net::stream<net::tcp_source>& req,
	const net::tcp_connection& tcp_conn
);

static net::action_status show_scoreboard(
	net::stream<net::tcp_source>& req,
	const net::tcp_connection& tcp_conn
);

int main(int argc, char** argv) {
	int argi = 1;
	bool read_gsport = false;
	bool read_verbose = false;
	std::string port = DEFAULT_PORT;
	while (argi <= argc - 1) {
		std::string_view arg{argv[argi]};
		if (arg == "-p") {
			if (read_gsport) {
				std::cout << "Can only set port once.\n";
				return 1;
			}
			if (argi + 1 == argc) {
				std::cout << "Please specify the port after -p.\n";
				return 1;
			}
			port = argv[argi + 1];
			argi += 2;
			read_gsport = true;
			continue;
		}
		if (arg == "-v") {
			if (read_verbose) {
				std::cout << "Duplicated -v.\n";
				return 1;
			}
			verbose::set(true);
			argi++;
			continue;
		}
		std::cout << "Unknown CLI argument.\n";
		return 1;
	}

	if (setup_directories() != 0) {
		std::cout << "Failed to setup the " << DEFAULT_GAME_DIR << " directory.\n";
		std::cout << "Shutting down.\n";
		return 1;
	}

	if (signal(SIGPIPE, SIG_IGN) != 0) {
		std::cout << "Failed to ignore SIGPIPE.\n";
		return 1;
	}
	if (signal(SIGCHLD, SIG_IGN) != 0) {
		std::cout << "Failed to ignore SIGCHDL.\n";
		return 1;
	}

	net::udp_connection udp_conn{{port, SOCK_DGRAM}};
	if (!udp_conn.valid()) {
		std::cout << "Failed to open udp connection at " << DEFAULT_PORT << ".\n";
		return 1;
	}
	net::tcp_server tcp_sv{{port, SOCK_STREAM}};
	if (!tcp_sv.valid()) {
		std::cout << "Failed to open tcp connection at " << DEFAULT_PORT << ".\n";
		return 1;
	}

	std::srand(std::time(nullptr));
	udp_action_map udp_actions;
	udp_actions.add_action("SNG", start_new_game);
	udp_actions.add_action("QUT", end_game);
	udp_actions.add_action("DBG", start_new_game_debug);
	udp_actions.add_action("TRY", do_try);
	tcp_action_map tcp_actions;
	tcp_actions.add_action("STR", show_trials);
	tcp_actions.add_action("SSB", show_scoreboard);

	fd_set r_fds;
	int max_fd = udp_conn.get_fildes();
	if (tcp_sv.get_fildes() > max_fd)
		max_fd = tcp_sv.get_fildes();
	while (!exit_server) {
		FD_ZERO(&r_fds);
		FD_SET(udp_conn.get_fildes(), &r_fds);
		FD_SET(tcp_sv.get_fildes(), &r_fds);
		int counter = select(max_fd + 1, &r_fds, nullptr, nullptr, nullptr);
		if (counter <= 0) {
			std::cout << "Failed to select the udp/tcp connection.\n";
			return 1;
		}
		net::action_status status[2] = {net::action_status::OK, net::action_status::OK};
		if (FD_ISSET(udp_conn.get_fildes(), &r_fds))
			status[0] = handle_udp(udp_conn, udp_actions);
		if (FD_ISSET(tcp_sv.get_fildes(), &r_fds))
			status[1] = handle_tcp(tcp_sv, tcp_actions);
		for (auto stat : status)
			if (stat != net::action_status::OK)
				std::cerr << net::status_to_message(stat) << '.' << std::endl;
	}
	return 0;
}

static net::action_status handle_udp(net::udp_connection& udp_conn, const udp_action_map& actions) {
	net::other_address client_addr;
	auto [status, request] = udp_conn.listen(client_addr);
	if (status != net::action_status::OK)
		return status;
	status = actions.execute(request, udp_conn, client_addr);
	if (status == net::action_status::UNK_ACTION) {
		verbose::write(client_addr, "unknown request", "?");
		net::out_stream out;
		out.write("ERR").prime();
		return udp_conn.answer(out, client_addr);
	}
	return status;
}

static net::action_status handle_tcp(net::tcp_server& tcp_sv, const tcp_action_map& actions) {
	net::other_address client_addr;
	auto [status, tcp_conn] = tcp_sv.accept_client(client_addr);
	if (status != net::action_status::OK)
		return status;
	pid_t pid = fork();
	if (pid == -1)
		return net::action_status::ERR;
	if (pid != 0)
		return net::action_status::OK;
	// TODO: fork
	net::stream<net::tcp_source> request = tcp_conn.to_stream();
	status = actions.execute(request, tcp_conn);
	if (status == net::action_status::UNK_ACTION) {
		verbose::write(client_addr, "unknown request", "?");
		net::out_stream out;
		out.write("ERR").prime();
		status = tcp_conn.answer(out);
	}
	if (status != net::action_status::OK)
		exit(EXIT_FAILURE);
	exit(EXIT_SUCCESS);
}

static net::action_status start_new_game(net::stream<net::udp_source>& req,
										 const net::udp_connection& udp_conn,
										 const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RSG");
	auto [status, fields] = req.read({{4, 6}, {1, 3}});
	if (status != net::action_status::OK
		|| (status = req.check_strict_end()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		verbose::write(client_addr, "malformed start request", "?");
		return udp_conn.answer(out_strm, client_addr);
	}
	status = net::is_valid_plid(fields[0]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		verbose::write(
			client_addr, "malformed player id",
			"PLID=", fields[0],
			", DURATION=", fields[1]
		);
		return udp_conn.answer(out_strm, client_addr);
	}
	
	status = net::is_valid_max_playtime(fields[1]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		verbose::write(
			client_addr, "malformed duration",
			"PLID=", fields[0],
			", DURATION=", fields[1]
		);
		return udp_conn.answer(out_strm, client_addr);
	}

	auto res = game::find_active(fields[0].c_str());
	if (res.first != net::action_status::NOT_IN_GAME) {
		out_strm.write("NOK").prime(); // what to send on server failure?
		verbose::write(
			client_addr, "game already underway",
			"PLID=", fields[0],
			", DURATION=", fields[1]
		);
		if (res.first == net::action_status::OK)
			return udp_conn.answer(out_strm, client_addr);
		udp_conn.answer(out_strm, client_addr);
		return res.first;
	}

	res = game::create(fields[0].c_str(), std::stoul(fields[1]));
	if (res.first != net::action_status::OK) {
		verbose::write(
			client_addr, "failed to create game",
			"PLID=", fields[0],
			", DURATION=", fields[1]
		);
		out_strm.write("NOK").prime(); // TODO: what to send in case of failure?
		udp_conn.answer(out_strm, client_addr);
		return res.first;
	}
	out_strm.write("OK").prime();
	verbose::write(
		client_addr, "created new game",
		"PLID=", fields[0],
		", DURATION=", fields[1]
	);
	return udp_conn.answer(out_strm, client_addr);
}

static net::action_status end_game(net::stream<net::udp_source>& req,
								   const net::udp_connection& udp_conn,
								   const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RQT");
	auto [status, plid] = req.read(6, 6);
	if (status != net::action_status::OK
		|| (status = req.check_strict_end()) != net::action_status::OK
		|| (status = net::is_valid_plid(plid)) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	auto [gm_status, gm] = game::find_active(plid.c_str());
	if (gm_status != net::action_status::OK) {
		out_strm.write("NOK").prime(); // what to send on fs failure?
		std::cout << out_strm.view();
		if (gm_status == net::action_status::NOT_IN_GAME)
			return udp_conn.answer(out_strm, client_addr);
		udp_conn.answer(out_strm, client_addr);
		return gm_status;
	}
	gm_status = gm.quit();
	if (gm_status != net::action_status::OK) {
		out_strm.write("NOK").prime(); // what to send in case of sv failure?
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return gm_status;
	}
	out_strm.write("OK");
	for (int i = 0; i < GUESS_SIZE; i++)
		out_strm.write(gm.secret_key()[i]);
	out_strm.prime();
	std::cout << out_strm.view();
	return udp_conn.answer(out_strm, client_addr);
}

static net::action_status start_new_game_debug(net::stream<net::udp_source>& req,
										 const net::udp_connection& udp_conn,
										 const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RDB");
	auto [status, fields] = req.read({{4, 6}, {1, 3}});
	if (status != net::action_status::OK) {
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
	if ((status != net::action_status::OK)
		|| (status = req.check_strict_end()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	auto res = game::find_active(fields[0].c_str());
	if (res.first != net::action_status::NOT_IN_GAME) {
		out_strm.write("NOK").prime(); // what to send on server failure?
		std::cout << out_strm.view();
		if (res.first == net::action_status::OK)
			return udp_conn.answer(out_strm, client_addr);
		udp_conn.answer(out_strm, client_addr);
		return res.first;
	}

	res = game::create(fields[0].c_str(), std::stoul(fields[1]), secret_key);
	if (res.first != net::action_status::OK) {
		out_strm.write("NOK").prime(); // what to send in case of failure?
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return res.first;
	}
	out_strm.write("OK").prime();
	std::cout << out_strm.view();
	return udp_conn.answer(out_strm, client_addr);
}

static net::action_status do_try(net::stream<net::udp_source>& req,
										 const net::udp_connection& udp_conn,
										 const net::other_address& client_addr) {
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
		|| (res.first = req.check_strict_end()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	trial = res.second[0];
	auto gm = game::find_active(plid.c_str());
	if (gm.first != net::action_status::OK) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		if (gm.first == net::action_status::NOT_IN_GAME)
			return udp_conn.answer(out_strm, client_addr);
		udp_conn.answer(out_strm, client_addr);
		return gm.first;
	}
	char duplicate_at = gm.second.is_duplicate(play);
	if (trial != gm.second.current_trial() + 1) {
		if (trial == gm.second.current_trial() && duplicate_at == gm.second.current_trial()) {
			out_strm.write("OK");
			out_strm.write(gm.second.current_trial());
			out_strm.write(gm.second.last_trial()->nB + '0');
			out_strm.write(gm.second.last_trial()->nW + '0').prime();
			std::cout << out_strm.view();
			return udp_conn.answer(out_strm, client_addr);
		}

		out_strm.write("INV").prime();
		std::cout << out_strm.view();
		// TODO: persist game? forcibly remove game?
		return udp_conn.answer(out_strm, client_addr);
	}

	if (duplicate_at != MAX_TRIALS + 1) {
		out_strm.write("DUP").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	auto [play_status, play_res] = gm.second.guess(play);
	if (play_status != net::action_status::OK) {
		out_strm.write("INV").prime(); // TODO: unsure if it should return INV in this case
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return play_status; // return first error
	}
	if (play_res == game::result::LOST_TIME || play_res == game::result::LOST_TRIES) {
		if (play_res == game::result::LOST_TIME)
			out_strm.write("ETM");
		else
			out_strm.write("ENT");
		for (int i = 0; i < GUESS_SIZE; i++)
			out_strm.write(gm.second.secret_key()[i]);
		out_strm.prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	out_strm.write("OK");
	out_strm.write(gm.second.current_trial());
	out_strm.write(gm.second.last_trial()->nB + '0');
	out_strm.write(gm.second.last_trial()->nW + '0').prime();
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
		(status = req.check_strict_end()) != net::action_status::OK ||
		(status = net::is_valid_plid(plid)) != net::action_status::OK) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return tcp_conn.answer(out_strm);
	}

	auto [gm_stat, gm] = game::find_any(plid.c_str());
	if (gm_stat != net::action_status::OK) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		if (gm_stat == net::action_status::NOT_IN_GAME)
			return tcp_conn.answer(out_strm);
		tcp_conn.answer(out_strm); // TODO: what to send here
		return gm_stat;
	}

	auto res = gm.has_ended();
	if (res.first != net::action_status::OK) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		tcp_conn.answer(out_strm); // TODO: what to send here
		return res.first;
	}
	auto [out_stat, out] = gm.to_string();
	if (out_stat != net::action_status::OK) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		tcp_conn.answer(out_strm); // TODO: what to send here
		return res.first;
	}
	if (res.second != game::result::ONGOING)
		out_strm.write("FIN");
	else
		out_strm.write("ACT");
	out_strm.write("STATE_" + plid + ".txt"); // TODO: maybe move this to game
	if (out.size() > 1024) // TODO: what here=
		return net::action_status::ERR;
	// TODO: check filename at client
	out_strm.write(std::to_string(out.size()));
	out_strm.write(out).prime();
	std::cout << out_strm.view();
	return tcp_conn.answer(out_strm);
}

static net::action_status show_scoreboard(net::stream<net::tcp_source>& req,
										  const net::tcp_connection& tcp_conn) {
	auto [sb_stat, sb] = scoreboard::get_latest();
	if (sb_stat != net::action_status::OK)
		return sb_stat;
	net::out_stream out_strm;
	out_strm.write("RSS");
	if (sb.empty()) {
		out_strm.write("EMPTY").prime();
		std::cout << out_strm.view();
		return tcp_conn.answer(out_strm);
	}
	auto [f_stat, file] = sb.to_string();
	if (f_stat != net::action_status::OK)
		return f_stat;
	if (file.size() > 1024) // TODO: what here?
		return net::action_status::ERR;
	out_strm.write("OK");
	out_strm.write("SCOREBOARD.txt");
	out_strm.write(std::to_string(file.size()));
	out_strm.write(file).prime();
	return tcp_conn.answer(out_strm);
}
