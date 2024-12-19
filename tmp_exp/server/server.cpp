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
			return;
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


static void sigint_handler(int signal) {
	exit_server = true;
}


bool verbose::_mode{false};

using udp_action_map = net::action_map<
	net::udp_source,
	const net::udp_connection&,
	const net::other_address&
>;

using tcp_action_map = net::action_map<
	net::tcp_source,
	const net::tcp_connection&,
	const net::other_address&
>;

static void handle_udp(net::udp_connection& udp_conn, const udp_action_map& actions);
static void handle_tcp(net::tcp_server& tcp_sv, const tcp_action_map& actions);

static void start_new_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static void end_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static void start_new_game_debug(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static void do_try(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static void show_trials(
	net::stream<net::tcp_source>& req,
	const net::tcp_connection& tcp_conn,
	const net::other_address& client_addr
);

static void show_scoreboard(
	net::stream<net::tcp_source>& req,
	const net::tcp_connection& tcp_conn,
	const net::other_address& client_addr
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
	if (signal(SIGINT, sigint_handler)) {
		std::cout << "Failed to set SIGINT handler.\n";
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
			std::cout << "Failed to select the udp/tcp connection\n";
			break;
		}
		try {
			if (FD_ISSET(udp_conn.get_fildes(), &r_fds))
				handle_udp(udp_conn, udp_actions);
		} catch (net::socket_closed_error& err) { // ignore (client closed early)
		} catch (net::socket_error& err) {
			std::cout << "Socket error" << err.what() << "(terminating)\n";
			break;
		} catch (net::system_error& err) {
			std::cout << "System error: " << err.what() << "(terminating)\n";
			break;
		} catch (net::io_error& err) {
			std::cout << "IO error: " << err.what() << "(terminating)\n";
			break;
		} catch (net::corruption_error& err) {
			std::cout << "Server corruption: " << err.what() << "(ignoring)\n";
		} catch (std::exception& err) {
			std::cout << "Unexpected exception: " << err.what() << "(terminating)\n";
			break;
		} catch (...) {
			std::cout << "Unknown exception (terminating)\n";
			break;
		}
		try {
			if (FD_ISSET(tcp_sv.get_fildes(), &r_fds))
				handle_tcp(tcp_sv, tcp_actions);
		} catch (net::socket_closed_error& err) { // ignore (client closed early)
		} catch (net::socket_error& err) {
			std::cout << "Socket error" << err.what() << "(terminating)\n";
			break;
		} catch (net::system_error& err) {
			std::cout << "System error: " << err.what() << "(terminating)\n";
			break;
		} catch (net::io_error& err) {
			std::cout << "IO error: " << err.what() << "(terminating)\n";
			break;
		} catch (net::corruption_error& err) {
			std::cout << "Server corruption: " << err.what() << "(ignoring)\n";
		} catch (std::exception& err) {
			std::cout << "Unexpected exception: " << err.what() << "(terminating)\n";
			break;
		} catch (...) {
			std::cout << "Unknown exception (terminating)\n";
			break;
		}
	}
	return 0;
}

static void handle_udp(net::udp_connection& udp_conn, const udp_action_map& actions) {
	net::other_address client_addr;
	auto request = udp_conn.listen(client_addr);
	try {
		actions.execute(request, udp_conn, client_addr);
	} catch (net::syntax_error& err) { // unknown req
		verbose::write(client_addr, "unknown request", "?");
		net::out_stream out;
		out.write("ERR").prime();
		udp_conn.answer(out, client_addr);
	}
}

static void handle_tcp(net::tcp_server& tcp_sv, const tcp_action_map& actions) {
	net::other_address client_addr;
	auto tcp_conn = tcp_sv.accept_client(client_addr);
	pid_t pid = fork();
	if (pid == -1)
		throw net::system_error{"Failed to fork for tcp client"};
	if (pid != 0)
		return;
	exit_server = true;
	net::stream<net::tcp_source> request = tcp_conn.to_stream();
	try {
		actions.execute(request, tcp_conn, client_addr);
	} catch (net::syntax_error& err) {
		verbose::write(client_addr, "unknown request", "?");
		net::out_stream out;
		out.write("ERR").prime();
		tcp_conn.answer(out);
	} catch (std::exception&  err) {
		std::cout << "Child tcp process encountered an exception: " << err.what() << '\n';
	}
}

static void start_new_game(net::stream<net::udp_source>& req,
							const net::udp_connection& udp_conn,
							const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RSG");
	net::message fields;
	try {
		fields = req.read({{PLID_SIZE, PLID_SIZE}, {1, MAX_PLAYTIME_SIZE}});
		req.check_strict_end();
	} catch (net::interaction_error& err) {
		out_strm.write("ERR").prime();
		verbose::write(client_addr, "malformed start request", "?");
		udp_conn.answer(out_strm, client_addr);
		return;
	}
	if (!net::is_valid_plid(fields[0])) {
		out_strm.write("ERR").prime();
		verbose::write(
			client_addr, "malformed player id",
			"PLID=", fields[0],
			", DURATION=", fields[1]
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}
	
	if (!net::is_valid_max_playtime(fields[1])) {
		out_strm.write("ERR").prime();
		verbose::write(
			client_addr, "malformed duration",
			"PLID=", fields[0],
			", DURATION=", fields[1]
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}

	game res;
	try {
		res = game::create(fields[0].c_str(), std::stoul(fields[1]));
	} catch (net::game_error& err) {
		out_strm.write("NOK").prime();
		verbose::write(
			client_addr, "game already underway",
			"PLID=", fields[0],
			", DURATION=", fields[1]
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}
	out_strm.write("OK").prime();
	verbose::write(
		client_addr, "created new game",
		"PLID=", fields[0],
		", DURATION=", fields[1]
	);
	udp_conn.answer(out_strm, client_addr);
}

static void end_game(net::stream<net::udp_source>& req,
					const net::udp_connection& udp_conn,
					const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RQT");
	net::field plid;
	try {
		plid = req.read(PLID_SIZE, PLID_SIZE);
		req.check_strict_end();
	} catch (net::interaction_error& err) {
		out_strm.write("ERR").prime();
		verbose::write(client_addr, 
			"malformed quit request",
			"?"
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}
	if (!net::is_valid_plid(plid)) {
		out_strm.write("ERR").prime();
		verbose::write(client_addr, 
			"invalid plid in quit request",
			"PLID=", plid
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}

	game gm;
	try {
		gm = game::find_active(plid.c_str());
		if (gm.has_ended() != game::result::ONGOING)
			throw net::game_error{"No active games"};
	} catch (net::game_error& err) {
		out_strm.write("NOK").prime();
		verbose::write(
			client_addr, "plid did not have an ongoing game for quit request",
			"PLID=", plid
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}

	try {
		gm.quit();
	} catch (net::game_error& err) {
		out_strm.write("NOK").prime();
		verbose::write(
			client_addr, "game in active directory was unexpectedly terminated",
			"PLID=", plid
		);
		udp_conn.answer(out_strm, client_addr);
		throw net::corruption_error{"Game in active directory was unexpectedly terminated"};
	}
	out_strm.write("OK");
	for (int i = 0; i < GUESS_SIZE; i++)
		out_strm.write(gm.secret_key()[i]);
	out_strm.prime();
	verbose::write(
		client_addr, "quit game",
		"PLID=", plid
	);
	udp_conn.answer(out_strm, client_addr);
}

static void start_new_game_debug(net::stream<net::udp_source>& req,
								const net::udp_connection& udp_conn,
								const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RDB");
	net::message fields;
	try {
		fields = req.read({{PLID_SIZE, PLID_SIZE}, {1, MAX_PLAYTIME_SIZE}});
	} catch (net::interaction_error& err) {
		out_strm.write("ERR").prime();
		verbose::write(client_addr, "malformed debug request", "?");
		udp_conn.answer(out_strm, client_addr);
		return;
	}
	if (!net::is_valid_plid(fields[0])) {
		out_strm.write("ERR").prime();
		verbose::write(client_addr,
			"malformed player id",
			"PLID=", fields[0]
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}
	if (!net::is_valid_max_playtime(fields[1])) {
		out_strm.write("ERR").prime();
		verbose::write(client_addr,
			"malformed duration",
			"PLID=", fields[0],
			", DURATION=", fields[1]
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}
	char secret_key[GUESS_SIZE];
	for (int i = 0; i< GUESS_SIZE; i++) {
		net::field col;
		try {
			col = req.read(1, 1);
			if (!net::is_valid_color(col))
				throw net::syntax_error{"Bad color"};
		} catch (net::interaction_error& err) {
			out_strm.write("ERR").prime();
			secret_key[i] = '\0';
			verbose::write(client_addr,
				"malformed color",
				"PLID=", fields[0],
				", DURATION=", fields[1],
				", CODE[0:", i, "]=", secret_key
			);
			udp_conn.answer(out_strm, client_addr);
			return;
		}
		secret_key[i] = col[0];
	}
	try {
		req.check_strict_end();
	} catch (net::interaction_error& error) {
		out_strm.write("ERR").prime();
		verbose::write(
			client_addr, "malformed end of request",
			"PLID=", fields[0],
			", DURATION=", fields[1],
			", CODE=", std::string_view{secret_key, GUESS_SIZE}
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}

	game res;
	try {
		res = game::create(fields[0].c_str(), std::stoul(fields[1]), secret_key);
	} catch (net::game_error& err) {
		out_strm.write("NOK").prime();
		verbose::write(
			client_addr, "game already underway",
			"PLID=", fields[0],
			", DURATION=", fields[1],
			", CODE=", std::string_view{secret_key, GUESS_SIZE}
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}
	out_strm.write("OK").prime();
	verbose::write(
		client_addr, "created new game",
		"PLID=", fields[0],
		", DURATION=", fields[1],
		", CODE=", std::string_view{secret_key, GUESS_SIZE}
	);
	udp_conn.answer(out_strm, client_addr);
}

static void do_try(net::stream<net::udp_source>& req,
					const net::udp_connection& udp_conn,
					const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RTR");
	net::field plid;
	try {
		plid = req.read(PLID_SIZE, PLID_SIZE);
	} catch (net::interaction_error& err) {
		out_strm.write("ERR").prime();
		verbose::write(client_addr, 
			"malformed try request",
			"?"
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}
	if (!net::is_valid_plid(plid)) {
		out_strm.write("ERR").prime();
		verbose::write(client_addr, 
			"invalid plid",
			"PLID=", plid
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}

	char play[GUESS_SIZE];
	for (int i = 0; i< GUESS_SIZE; i++) {
		net::field col;
		try {
			col = req.read(1, 1);
			if (!net::is_valid_color(col))
				throw net::syntax_error{"Bad color"};
		} catch (net::interaction_error& err) {
			play[i] = '\0';
			out_strm.write("ERR").prime();
			verbose::write(client_addr, 
				"invalid guess color",
				"PLID=", plid,
				", GUESS[0:", i, "]=", play
			);
			udp_conn.answer(out_strm, client_addr);
			return;
		}
		play[i] = col[0];
	}

	char trial;
	try {
		trial = req.read(1, 1)[0];
		req.check_strict_end();
	} catch (net::interaction_error& err) {
		out_strm.write("ERR").prime();
		verbose::write(client_addr, 
			"could not read trial number/incorrect message ending",
			", PLID=", plid,
			", GUESS=", std::string_view{play, GUESS_SIZE}
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}

	game gm;
	try {
		gm = game::find_active(plid.c_str());
	} catch (net::game_error& err) {
		out_strm.write("NOK").prime();
		verbose::write(client_addr, 
			"plid did not have an ongoing game",
			"PLID=", plid,
			", GUESS=", std::string_view{play, GUESS_SIZE},
			", TRIAL_NUMBER=", trial
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}

	if (gm.has_ended() == game::result::LOST_TIME) {
		out_strm.write("ETM");
		for (int i = 0; i < GUESS_SIZE; i++)
			out_strm.write(gm.secret_key()[i]);
		out_strm.prime();
		verbose::write(client_addr, 
			"either maximum time achieved",
			"PLID=", plid,
			", GUESS=", std::string_view{play, GUESS_SIZE},
			", TRIAL_NUMBER=", trial
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}

	char duplicate_at = gm.is_duplicate(play);
	if (trial != gm.current_trial() + 1) {
		if (trial == gm.current_trial() && duplicate_at == gm.current_trial()) {
			out_strm.write("OK");
			out_strm.write(gm.current_trial());
			out_strm.write(gm.last_trial()->nB + '0');
			out_strm.write(gm.last_trial()->nW + '0').prime();
			verbose::write(client_addr, 
				"resend identified, number of trials not increased",
				"PLID=", plid,
				", GUESS=", std::string_view{play, GUESS_SIZE},
				", TRIAL_NUMBER=", trial
			);
			udp_conn.answer(out_strm, client_addr);
			return;
		}

		out_strm.write("INV").prime();
		verbose::write(client_addr, 
			"invalid trial request",
			"PLID=", plid,
			", GUESS=", std::string_view{play, GUESS_SIZE},
			", TRIAL_NUMBER=", trial
		);
		gm.quit(); // terminate game
		udp_conn.answer(out_strm, client_addr);
		return;
	}

	if (duplicate_at != MAX_TRIALS + 1) {
		out_strm.write("DUP").prime();
		verbose::write(client_addr, 
			"duplicated guess received",
			"PLID=", plid,
			", GUESS=", std::string_view{play, GUESS_SIZE},
			", TRIAL_NUMBER=", trial
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}

	game::result play_res = gm.guess(play);
	if (play_res == game::result::LOST_TIME || play_res == game::result::LOST_TRIES) {
		if (play_res == game::result::LOST_TIME)
			out_strm.write("ETM");
		else
			out_strm.write("ENT");
		for (int i = 0; i < GUESS_SIZE; i++)
			out_strm.write(gm.secret_key()[i]);
		out_strm.prime();
		verbose::write(client_addr, 
			"either maximum time achieved or no more trials are available",
			"PLID=", plid,
			", GUESS=", std::string_view{play, GUESS_SIZE},
			", TRIAL_NUMBER=", trial
		);
		udp_conn.answer(out_strm, client_addr);
		return;
	}
	out_strm.write("OK");
	out_strm.write(gm.current_trial());
	out_strm.write(gm.last_trial()->nB + '0');
	out_strm.write(gm.last_trial()->nW + '0').prime();
	verbose::write(client_addr, 
			"try request sucessfully received",
			"PLID=", plid,
			", GUESS=", std::string_view{play, GUESS_SIZE},
			", TRIAL_NUMBER=", trial
		);
	udp_conn.answer(out_strm, client_addr);
}

static void show_trials(net::stream<net::tcp_source>& req,
									  const net::tcp_connection& tcp_conn,
									  const net::other_address& client_addr) {
	net::field plid;
	net::out_stream out_strm;
	out_strm.write("RST");
	try {
		plid = req.read(PLID_SIZE, PLID_SIZE);
		req.check_strict_end();
	} catch (net::interaction_error& err) {
		out_strm.write("NOK").prime();
		verbose::write(client_addr, 
			"malformed show trials request",
			"?"
		);
		tcp_conn.answer(out_strm);
		return;
	}
	if (!net::is_valid_plid(plid)) {
		out_strm.write("NOK").prime();
		verbose::write(client_addr, 
			"malformed plid",
			"PLID=", plid
		);
		tcp_conn.answer(out_strm);
		return;
	}

	game gm;
	try {
		gm = game::find_any(plid.c_str());
	} catch (net::game_error& err) {
		out_strm.write("NOK").prime();
		verbose::write(client_addr, 
			"no recorded games for this player",
			"PLID=", plid
		);
		tcp_conn.answer(out_strm);
		return;
	}

	game::result res = gm.has_ended();
	std::string out = gm.to_string();
	if (res != game::result::ONGOING)
		out_strm.write("FIN");
	else
		out_strm.write("ACT");
	out_strm.write("STATE_" + plid + ".txt");
	out_strm.write(std::to_string(out.size()));
	out_strm.write(out).prime();
	verbose::write(client_addr, 
			"list of previously made trials sent",
			"PLID=", plid
		);
	tcp_conn.answer(out_strm);
	return;
}

static void show_scoreboard(net::stream<net::tcp_source>& req,
							const net::tcp_connection& tcp_conn,
							const net::other_address& client_addr) {
	req.check_strict_end();
	scoreboard sb = scoreboard::get_latest();
	net::out_stream out_strm;
	out_strm.write("RSS");
	if (sb.empty()) {
		out_strm.write("EMPTY").prime();
		verbose::write(client_addr,
			"no game was yet won by any player",
			"show_scoreboard"
		);
		tcp_conn.answer(out_strm);
		return;
	}
	net::field file = sb.to_string();
	out_strm.write("OK");
	out_strm.write("SB_" + sb.start_time() + ".txt");
	out_strm.write(std::to_string(file.size()));
	out_strm.write(file).prime();
	verbose::write(client_addr, 
		"scoreboard sent",
		"show_scoreboard"
	);
	tcp_conn.answer(out_strm);
}
