#include "../common/common.hpp"
#include <iostream>
#include <cstring>
#include <fstream>

#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT "58016"
#define TIMEOUT 5
#define DEFAULT_ERR_MSG "ERR reply from server"

static bool in_game = false;
static bool exit_app = false;
static char current_plid[PLID_SIZE];
static char current_trial = '0';

static void do_start(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static void do_try(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static void do_quit(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static void do_exit(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static void do_debug(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static void do_show_trials(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static void do_scoreboard(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);

int main(int argc, char** argv) {
	int argi = 1;
	bool read_gsip = false;
	bool read_gsport = false;
	std::string host = DEFAULT_HOST;
	std::string port = DEFAULT_PORT;
	while (argi <= argc - 1) {
		std::string_view arg{argv[argi]};
		if (arg == "-n") {
			if (read_gsip) {
				std::cout << "Can only set host once." << std::endl;
				return 1;
			}
			if (argi + 1 == argc) {
				std::cout << "Please specify the host after -n." << std::endl;
				return 1;
			}
			host = argv[argi + 1];
			argi += 2;
			read_gsip = true;
			continue;
		}
		if (arg == "-p") {
			if (read_gsport) {
				std::cout << "Can only set port once." << std::endl;
				return 1;
			}
			if (argi + 1 == argc) {
				std::cout << "Please specify the port after -p." << std::endl;
				return 1;
			}
			port = argv[argi + 1];
			argi += 2;
			read_gsport = true;
			continue;
		}
		std::cout << "Unknown CLI argument." << std::endl;
		return 1;
	}

	net::udp_connection udp{net::self_address{host, port, SOCK_DGRAM}};
	if (!udp.valid()) {
		std::cout << "Failed to open udp connection. Check if the address and port are correct." << std::endl;
		return 1;
	}
	net::self_address tcp_addr{net::self_address{host, port, SOCK_STREAM}};

	net::action_map<net::file_source, net::udp_connection&, const net::self_address&> actions;
	actions.add_action("start", do_start);
	actions.add_action("try", do_try);
	actions.add_action({"show_trials", "st"}, do_show_trials);
	actions.add_action({"scoreboard", "sb"}, do_scoreboard);
	actions.add_action("quit", do_quit);
	actions.add_action("exit", do_exit);
	actions.add_action("debug", do_debug);

	while (!exit_app) {
		net::stream<net::file_source> strm{STDIN_FILENO, false};
		try {
			actions.execute(strm, udp, tcp_addr);
		} catch (net::syntax_error& err) {
			std::cout << "Wrong syntax: " << err.what() << '\n';
		} catch (net::formatting_error& err) {
			std::cout << "Wrong format: " << err.what() << '\n';
		} catch (net::game_error& err) {
			std::cout << err.what() << '\n';
		} catch (net::bad_response& err) {
			std::cout << "Bad server response: " << err.what() << "(terminating)\n";
			break;
		} catch (net::socket_error& err) {
			std::cout << "Bad socket: " << err.what() << " (terminating)\n";
			break;
		} catch (net::io_error& err) {
			std::cout << "IO error: " << err.what() << " (terminating)\n";
			break;
		} catch (...) {
			std::cout << "Unexpected exception (terminating)\n";
			break;
		}
		try {
			strm.exhaust();
		} catch (net::syntax_error& err) {
			std::cout << "Wrong syntax: " << err.what() << '\n';
		} catch (net::formatting_error& err) {
			std::cout << "Wrong format: " << err.what() << '\n';
		} catch (net::game_error& err) {
			std::cout << err.what() << '\n';
		} catch (net::bad_response& err) {
			std::cout << "Bad server response: " << err.what() << "(terminating)\n";
			break;
		} catch (net::socket_error& err) {
			std::cout << "Bad socket: " << err.what() << " (terminating)\n";
			break;
		} catch (net::io_error& err) {
			std::cout << "IO error: " << err.what() << " (terminating)\n";
			break;
		} catch (...) {
			std::cout << "Unexpected exception (terminating)\n";
			break;
		}
		std::cout << std::flush;
	}
	std::cout << "Exiting the Player application...\n";
	return 0;
}

static void setup_game_clientside(const net::field& plid) {
	in_game = true;
	current_trial = '0';
	for (int i = 0; i < PLID_SIZE; i++)
		current_plid[i] = plid[i];
}

static void do_start(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	auto fields = msg.read({{PLID_SIZE, PLID_SIZE}, {1, MAX_PLAYTIME_SIZE}});
	if (!msg.no_more_fields())
		throw net::syntax_error{"start only takes PLID, DURATION"};
	if (!net::is_valid_plid(fields[0]))
		throw net::syntax_error{"Invalid plid"};
	if (!net::is_valid_max_playtime(fields[1]))
		throw net::syntax_error{"Invalid duration"};
	if (in_game)
		throw net::game_error{"Ongoing game"};

	net::out_stream out_strm;
	out_strm.write("SNG").write(fields[0]).write_and_fill(fields[1], MAX_PLAYTIME_SIZE, '0').prime();
	std::cout << "Sent buffer: \"" << out_strm.view() << '\"' << std::endl;

	net::other_address other;
	auto ans_strm = udp.request(out_strm, other);
	auto res = ans_strm.read(3, 3);
	if (res != "RSG") {
		if (res == "ERR") {
			ans_strm.check_strict_end();
			std::cout << DEFAULT_ERR_MSG << '\n';
			return;
		}
		throw net::bad_response{"Unknown reply"};
	}

	res = ans_strm.read(2, 3);
	ans_strm.check_strict_end();

	if (res == "OK") {
		setup_game_clientside(fields[0]);
		std::cout << "Successfully setup a new game (OK)\n";
		return;
	}

	if (res == "NOK") {
		std::cout << "Game with the given plid already underway (NOK)\n";
		return;
	}

	if (res == "ERR") {
		std::cout << "Server got incorrect start synteax (ERR)\n";
		return;
	}
	throw net::bad_response{"Unknown status"};
}

static void do_try(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	net::out_stream out_strm;
	out_strm.write("TRY");
	out_strm.write({current_plid, PLID_SIZE});
	net::field res;
	for (size_t i = 0; i < GUESS_SIZE; i++) {
		auto res = msg.read(1, 1);
		if (!net::is_valid_color(res))
			throw net::syntax_error{"Bad color at position " + std::to_string(i + 1)};
		out_strm.write(res[0]);
	}
	if (!msg.no_more_fields())
		throw net::syntax_error{"Try only takes X Y Z W"};
	if (!in_game)
		throw net::game_error{"Not in game"};
	out_strm.write(current_trial + 1).prime();

	std::cout << "Sent buffer: \"" << out_strm.view() << '\"' << std::endl;

	net::other_address other;
	auto ans_strm = udp.request(out_strm, other);
	res = ans_strm.read(3, 3);
	if (res != "RTR") {
		if (res == "ERR") {
			ans_strm.check_strict_end();
			std::cout << DEFAULT_ERR_MSG << '\n';
			return;
		}
		throw net::bad_response{"Unknown reply"};
	}

	res = ans_strm.read(2, 3);
	if (res == "DUP") {
		std::cout << "Duplicated guess (DUP)\n";
		return;
	}
	if (res == "INV") {
		in_game = false;
		std::cout << "Invalid trial (closing down game) (INV)\n";
		return;
	}
	if (res == "NOK") {
		in_game = false;
		std::cout << "No ongoing game (NOK)\n";
		return;
	}
	if (res == "ERR") {
		std::cout << "Server got wrong try syntax (ERR)\n";
		return;
	}

	bool is_ENT = res == "ENT";
	if (is_ENT || res == "ETM") {
		in_game = false;
		char correct_guess[2 * GUESS_SIZE];
		for (size_t i = 0; i < 2 * GUESS_SIZE; i += 2) {
			res = ans_strm.read(1, 1);
			if (!net::is_valid_color(res))
				throw net::bad_response{"Invalid color"};
			correct_guess[i] = res[0];
			correct_guess[i + 1] = ' ';
		}
		ans_strm.check_strict_end();
		correct_guess[2 * GUESS_SIZE - 1] = '\0';
		if (is_ENT)
			std::cout << "You ran out of tries!";
		else
			std::cout << "You ran out of time!";
		std::cout << " The secret key was " << correct_guess << "." << std::endl;
		return;
	}

	if (res != "OK")
		throw net::bad_response{"Unknown status"};

	current_trial++;
	char info[3];
	for (size_t i = 0; i < 3; i++) {
		res = ans_strm.read(1, 1);
		info[i] = res[0];
	}
	ans_strm.check_strict_end();
	if (info[2] < '0' || info[1] < '0' || info[0] < '1' || info[0] > '8')
		throw net::bad_response{"Illegal nT/nB/nW"};
	if (info[1] + info[2] - 2 * '0' > '0' + GUESS_SIZE) // nB + nW <= 4
		throw net::bad_response{"Illegal nB/nW (nB + nW > 4)"};
	if (info[1] == '0' + GUESS_SIZE) {
		in_game = false;
		std::cout << "You won in " << current_trial << " tries!" << std::endl;
		return;
	}
	std::cout << "You guessed " << info[1] << " colors in the correct place and";
	std::cout << " there were " << info[2] << " correct colors with an incorrect placement\n";
	return;
}

static void end_game(net::udp_connection& udp) {
	net::out_stream out_strm;
	out_strm.write("QUT").write({current_plid, PLID_SIZE}).prime();
	in_game = false;

	net::other_address other;
	auto ans_strm = udp.request(out_strm, other);
	auto res = ans_strm.read(3, 3);
	if (res != "RQT") {
		if (res == "ERR") {
			ans_strm.check_strict_end();
			std::cout << DEFAULT_ERR_MSG << '\n';
			return;
		}
		throw net::bad_response{"Unknown reply"};
	}
	
	res = ans_strm.read(2, 3);
	if (res == "ERR") {
		ans_strm.check_strict_end();
		std::cout << "Quit failed, quitting anyways (ERR)\n";
		return;
	}

	if (res == "NOK") {
		ans_strm.check_strict_end();
		std::cout << "Apparently no ongoing game (NOK)\n";
		return;
	}

	if (res != "OK")
		throw net::bad_response{"Unknown status"};

	in_game = false; // TODO: should probably quit even in case of error?
	char correct_guess[2 * GUESS_SIZE];
	for (size_t i = 0; i < 2 * GUESS_SIZE; i += 2) {
		res = ans_strm.read(1, 1);
		if (!net::is_valid_color(res))
			throw net::bad_response{"Invalid color"};
		correct_guess[i] = res[0];
		correct_guess[i + 1] = ' ';
	}
	ans_strm.check_strict_end();
	correct_guess[2 * GUESS_SIZE - 1] = '\0';
	std::cout << "You quit the game! The secret key was ";
	std::cout << correct_guess << '.' << std::endl;
	return;
}


static void do_quit(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	if (!msg.no_more_fields())
		throw net::syntax_error{"quit takes no arguments"};
	if (!in_game)
		throw net::game_error{"No ongoing game"};
	end_game(udp);
}

static void do_exit(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	if (!msg.no_more_fields())
		throw net::syntax_error{"exit takes no arguments"};
	if (in_game)
		end_game(udp);
	exit_app = true;
}

static void do_debug(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	auto res = msg.read({{PLID_SIZE, PLID_SIZE}, {1, MAX_PLAYTIME_SIZE}, {1, 1}, {1, 1}, {1, 1}, {1, 1}});
	if (!msg.no_more_fields())
		throw net::syntax_error{"debug only takes PLID DURATION X Y W Z"};
	if (!net::is_valid_plid(res[0]))
		throw net::syntax_error{"Invalid plid"};
	if (!net::is_valid_max_playtime(res[1]))
		throw net::syntax_error{"Invalid duration"};
	net::out_stream out_strm;
	out_strm.write("DBG");
	out_strm.write(res[0]);
	out_strm.write_and_fill(res[1], MAX_PLAYTIME_SIZE, '0');
	for (size_t i = 2; i < 2 + GUESS_SIZE; i++) {
		if (!net::is_valid_color(res[i]))
			throw net::syntax_error{"Invalid color at position " + std::to_string(i - 1)};
		out_strm.write(res[i]);
	}
	out_strm.prime();

	if (in_game)
		throw net::game_error{"Ongoing game"};

	std::cout << "Sent buffer: \"" << out_strm.view() << '\"' << std::endl;

	net::other_address other;
	auto ans_strm = udp.request(out_strm, other);

	auto ans_res = ans_strm.read(3, 3);
	if (ans_res != "RDB") {
		if (ans_res == "ERR") {
			ans_strm.check_strict_end();
			std::cout << DEFAULT_ERR_MSG << '\n';
			return;
		}
		throw net::bad_response{"Unknown reply"};
	}

	ans_res = ans_strm.read(2, 3);
	ans_strm.check_strict_end();

	if (ans_res == "OK") {
		setup_game_clientside(res[0]);
		std::cout << "Managed to setup new debug game (OK)\n";
		return;
	}

	if (ans_res == "NOK") {
		std::cout << "Ongoing game for the given plid (NOK)\n";
		return;
	}

	if (ans_res == "ERR") {
		std::cout << "Server got malformed message (ERR)\n";
		return;
	}
	throw net::bad_response{"Unknown status"};
}

static void read_file(net::stream<net::tcp_source>& ans_strm, net::field& name, net::field& file) {
	auto fld = ans_strm.read(1, MAX_FNAME_SIZE);
	if (!net::is_valid_fname(fld))
		throw net::bad_response{"Invalid filename"};
	name = std::move(fld);
	fld = ans_strm.read(1, MAX_FSIZE_LEN);
	size_t fsize = 0;
	try {
		fsize = std::stoul(fld.c_str());
	} catch (std::exception&) {
		throw net::bad_response{"Invalid file size"};
	}
	if (!net::is_valid_fsize(fsize))
		throw net::bad_response{"Invalid file size"};

	file = std::move(ans_strm.read(fsize, fsize, false));
}

static void write_file(const std::string& filename, const std::string& file) {
	std::ofstream stream;
	stream.open(filename, std::ofstream::out | std::ofstream::trunc);
	if (!stream)
		throw net::io_error{"Could not open write stream"};
	stream << file;
	if (!stream)
		throw net::io_error{"Could not write file"};
}

static void do_show_trials(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	if (!msg.no_more_fields())
		throw net::syntax_error{"show_trials does not take arguments"};
	if (!in_game)
		throw net::game_error{"No ongoing game"};
		
	net::out_stream out_strm;
	out_strm.write("STR").write({current_plid, PLID_SIZE}).prime();
	std::cout << "Sent buffer: \"" << out_strm.view() << '\"' << std::endl;

	net::tcp_connection tcp{tcp_addr};
	if (!tcp.valid())
		throw net::socket_error{"Could not open tcp socket"};
	auto ans_strm = tcp.request(out_strm);

	auto fld = ans_strm.read(3, 3);
	if (fld != "RST") {
		if (fld == "ERR") {
			ans_strm.check_strict_end();
			std::cout << DEFAULT_ERR_MSG << '\n';
			return;
		}
		throw net::bad_response{"Unknown reply"};
	}
	fld = ans_strm.read(3, 3);
	if (fld == "NOK") {
		ans_strm.check_strict_end();
		std::cout << "The specified user has no recorded games (or a problem may have occured)\n";
		return;
	}
	bool is_FIN = fld == "FIN";
	if (!is_FIN && fld != "ACT")
		throw net::bad_response{"Unknown status"};
	if (is_FIN)
		in_game = false;
	net::field fname, file;
	read_file(ans_strm, fname, file);
	ans_strm.check_strict_end();
	write_file(fname, file);
	std::cout << "Trial file:\n" << file << '\n';

}

static void do_scoreboard(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	if (!msg.no_more_fields())
		throw net::syntax_error{"scoreboard takes no arguments"};
		
	net::out_stream out_strm;
	out_strm.write("SSB").prime();
	std::cout << "Sent buffer: \"" << out_strm.view() << '\"' << std::endl;

	net::tcp_connection tcp{tcp_addr};
	if (!tcp.valid())
		throw net::socket_error{"Could not open tcp socket"};
	auto ans_strm = tcp.request(out_strm);
	auto fld = ans_strm.read(3, 3);
	if (fld != "RSS") {
		if (fld == "ERR") {
			ans_strm.check_strict_end();
			std::cout << DEFAULT_ERR_MSG << '\n';
			return;
		}
		throw net::bad_response{"Unknown reply"};
	}
	fld = ans_strm.read(2, 5);
	if (fld == "EMPTY") {
		ans_strm.check_strict_end();
		std::cout << "The scoreboard is empty\n";
		return;
	}
	if (fld != "OK")
		throw net::bad_response{"Unknown status"};
	net::field fname, file;
	read_file(ans_strm, fname, file);
	ans_strm.check_strict_end();
	write_file(fname, file);
	std::cout << "Scoreboard:\n" << file << std::endl;
}
