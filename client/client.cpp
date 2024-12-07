#include "../common/common.hpp"
#include <iostream>
#include <cstring>
#include <fstream>

#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT "58016"
#define TIMEOUT 5

static bool in_game = false;
static bool exit_app = false;
static char current_plid[PLID_SIZE];
static char current_trial = '0';

static net::action_status do_start(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static net::action_status do_try(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static net::action_status do_quit(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static net::action_status do_exit(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static net::action_status do_debug(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static net::action_status do_show_trials(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);
static net::action_status do_scoreboard(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr);

int main(int argc, char** argv) {
	int argi = 1;
	bool read_gsip = false;
	bool read_gsport = false;
	std::string host = DEFAULT_HOST;
	std::string port = DEFAULT_PORT;
	while (argi < argc - 1) {
		std::string_view arg{argv[argi]};
		if (arg == "-n") {
			if (read_gsip) {
				std::cout << "Can only set host once." << std::endl;
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
		auto status = actions.execute(strm, udp, tcp_addr);
		if (status != net::action_status::OK)
			std::cerr << net::status_to_message(status) << ".\n";
		status = strm.exhaust();
		if (status != net::action_status::OK)
			std::cerr << net::status_to_message(status) << ".\n";
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

static net::action_status do_start(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	auto [status, fields] = msg.read({{6, 6}, {1, 3}});
	if (status != net::action_status::OK || (status = msg.no_more_fields()) != net::action_status::OK)
		return status;
	
	status = net::is_valid_plid(fields[0]);
	if (status != net::action_status::OK)
		return status;

	status = net::is_valid_max_playtime(fields[1]);
	if (status != net::action_status::OK)
		return status;

	if (in_game)
		return net::action_status::ONGOING_GAME;

	net::out_stream out_strm;
	out_strm.write("SNG").write(fields[0]).write_and_fill(fields[1], 3, '0').prime();

	std::cout << "Sent buffer: \"" << out_strm.view() << '\"' << std::endl;

	net::other_address other;
	auto [req_stat, ans_strm] = udp.request(out_strm, other);
	if (req_stat != net::action_status::OK)
		return req_stat;

	auto res = ans_strm.read(3, 3);
	if (res.first != net::action_status::OK)
		return res.first;
	if (res.second != "RSG") {
		if (res.second == "ERR")
			return net::action_status::RET_ERR;
		return net::action_status::UNK_REPLY;
	}

	res = ans_strm.read(2, 3);
	if (res.first != net::action_status::OK ||
		(res.first = ans_strm.check_strict_end()) != net::action_status::OK)
		return res.first;

	if (res.second == "OK") {
		setup_game_clientside(fields[0]);
		return net::action_status::OK;
	}

	if (res.second == "NOK")
		return net::action_status::START_NOK;

	if (res.second == "ERR")
		return net::action_status::START_ERR;

	return net::action_status::UNK_STATUS;
}

static net::action_status do_try(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	net::out_stream out_strm;
	out_strm.write("TRY");
	out_strm.write(current_plid);
	std::pair<net::action_status, net::field> res;
	for (size_t i = 0; i < GUESS_SIZE; i++) {
		auto res = msg.read(1, 1);
		if (res.first != net::action_status::OK)
			return res.first;
		res.first = net::is_valid_color(res.second);
		if (res.first != net::action_status::OK)
			return res.first;
		out_strm.write(res.second);
	}
	if ((res.first = msg.no_more_fields()) != net::action_status::OK)
		return res.first;
	if (!in_game)
		return net::action_status::NOT_IN_GAME;
	out_strm.write(current_trial + 1).prime();

	std::cout << "Sent buffer: \"" << out_strm.view() << '\"' << std::endl;

	net::other_address other;
	auto [ans_stat, ans_strm] = udp.request(out_strm, other);
	if (ans_stat != net::action_status::OK)
		return ans_stat;
	
	res = ans_strm.read(3, 3);
	if (res.first != net::action_status::OK)
		return res.first;
	if (res.second != "RTR") {
		if (res.second == "ERR")
			return net::action_status::RET_ERR;
		return net::action_status::UNK_REPLY;
	}

	res = ans_strm.read(2, 3);
	if (res.first != net::action_status::OK)
		return res.first;

	if (res.second == "DUP")
		return net::action_status::TRY_DUP;
	if (res.second == "INV") {
		in_game = false;
		return net::action_status::TRY_INV;
	}
	if (res.second == "NOK") {
		in_game = false;
		return net::action_status::TRY_NOK;
	}
	if (res.second == "ERR")
		return net::action_status::TRY_ERR;

	bool is_ENT = res.second == "ENT";
	if (is_ENT || res.second == "ETM") {
		in_game = false;
		char correct_guess[2 * GUESS_SIZE];
		for (size_t i = 0; i < 2 * GUESS_SIZE; i += 2) {
			res = ans_strm.read(1, 1);
			if (res.first != net::action_status::OK)
				return res.first;
			res.first = net::is_valid_color(res.second);
			if (res.first != net::action_status::OK)
				return res.first;
			correct_guess[i] = res.second[0];
			correct_guess[i + 1] = ' ';
		}
		if ((res.first = ans_strm.check_strict_end()) != net::action_status::OK)
			return res.first;
		correct_guess[2 * GUESS_SIZE - 1] = '\0';
		if (is_ENT)
			std::cout << "You ran out of tries!";
		else
			std::cout << "You ran out of time!";
		std::cout << " The secret key was " << correct_guess << "." << std::endl;
		return net::action_status::OK;
	}

	if (res.second != "OK")
		return net::action_status::UNK_STATUS;

	current_trial++;
	char info[3];
	for (size_t i = 0; i < 3; i++) {
		res = ans_strm.read(1, 1);
		if (res.first != net::action_status::OK)
			return res.first;
		info[i] = res.second[0];
	}
	if ((res.first = ans_strm.check_strict_end()) != net::action_status::OK)
		return res.first;
	if (info[2] < '0' || info[1] < '0' || info[0] < '1' || info[0] > '8')
		return net::action_status::TRY_NT; // TODO err message
	if (info[1] + info[2] - 2 * '0' > '0' + GUESS_SIZE) // nB + nW <= 4
		return net::action_status::TRY_NT; // TODO err message
	if (info[1] == '0' + GUESS_SIZE) {
		in_game = false;
		std::cout << "You won in " << current_trial << " tries!" << std::endl;
		return net::action_status::OK;
	}
	std::cout << "You guessed " << info[1] << " colors in the correct place and";
	std::cout << " there were " << info[2] << " correct colors with an incorrect placement\n";
	return net::action_status::OK;
}

static net::action_status end_game(net::udp_connection& udp) {
	net::out_stream out_strm;
	out_strm.write("QUT").write(current_plid).prime();

	net::other_address other;
	auto [ans_stat , ans_strm] = udp.request(out_strm, other);
	if (ans_stat != net::action_status::OK)
		return ans_stat;

	auto res = ans_strm.read(3, 3);
	if (res.first != net::action_status::OK)
		return res.first;
	if (res.second != "RQT") {
		if (res.second == "ERR")
			return net::action_status::RET_ERR;
		return net::action_status::UNK_REPLY;
	}
	
	res = ans_strm.read(2, 3);
	if (res.first != net::action_status::OK)
		return res.first;

	if (res.second == "ERR")
		return net::action_status::QUIT_EXIT_ERR;

	if (res.second == "NOK") {
		in_game = false;
		return net::action_status::NOT_IN_GAME;
	}

	if (res.second != "OK")
		return net::action_status::UNK_STATUS;

	in_game = false; // TODO: should probably quit even in case of error?
	char correct_guess[2 * GUESS_SIZE];
	for (size_t i = 0; i < 2 * GUESS_SIZE; i += 2) {
		res = ans_strm.read(1, 1);
		if (res.first != net::action_status::OK)
			return res.first;
		res.first = net::is_valid_color(res.second);
		if (res.first != net::action_status::OK)
			return res.first;
		correct_guess[i] = res.second[0];
		correct_guess[i + 1] = ' ';
	}
	if ((res.first = ans_strm.check_strict_end()) != net::action_status::OK)
		return res.first;
	correct_guess[2 * GUESS_SIZE - 1] = '\0';
	std::cout << "You quit the game! The secret key was ";
	std::cout << correct_guess << '.' << std::endl;
	return net::action_status::OK;
}


static net::action_status do_quit(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	auto res = msg.no_more_fields();
	if (res != net::action_status::OK)
		return res;

	if (!in_game)
		return net::action_status::NOT_IN_GAME;
	return end_game(udp);
}

static net::action_status do_exit(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	auto res = msg.no_more_fields();
	if (res != net::action_status::OK)
		return res;

	if (!in_game) {
		exit_app = true;
		return net::action_status::OK;
	}

	exit_app = ((res = end_game(udp)) == net::action_status::OK);
	// TODO: should probably quit even in case of error?
	return res;
}

static net::action_status do_debug(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	auto res = msg.read({{PLID_SIZE, PLID_SIZE}, {1, 3}, {1, 1}, {1, 1}, {1, 1}, {1, 1}});
	if (res.first != net::action_status::OK || (res.first = msg.no_more_fields()) != net::action_status::OK)
		return res.first;
	if ((res.first = net::is_valid_plid(res.second[0])) != net::action_status::OK)
		return res.first;
	if ((res.first = net::is_valid_max_playtime(res.second[1])) != net::action_status::OK)
		return res.first;
	net::out_stream out_strm;
	out_strm.write("DBG");
	out_strm.write(res.second[0]);
	out_strm.write_and_fill(res.second[1], 3, '0');
	for (size_t i = 2; i < 2 + GUESS_SIZE; i++) {
		if ((res.first = net::is_valid_color(res.second[i])) != net::action_status::OK)
			return res.first;
		out_strm.write(res.second[i]);
	}
	out_strm.prime();

	if (in_game)
		return net::action_status::ONGOING_GAME;

	std::cout << "Sent buffer: \"" << out_strm.view() << '\"' << std::endl;

	net::other_address other;
	auto [ans_stat, ans_strm] = udp.request(out_strm, other);
	if (ans_stat != net::action_status::OK)
		return ans_stat;

	auto ans_res = ans_strm.read(3, 3);
	if (ans_res.first != net::action_status::OK)
		return ans_res.first;
	if (ans_res.second != "RDB") {
		if (ans_res.second == "ERR")
			return net::action_status::RET_ERR;
		return net::action_status::UNK_REPLY;
	}

	ans_res = ans_strm.read(2, 3);
	if (ans_res.first != net::action_status::OK ||
		(ans_res.first = ans_strm.check_strict_end()) != net::action_status::OK)
		return ans_res.first;

	if (ans_res.second == "OK") {
		setup_game_clientside(res.second[0]);
		return net::action_status::OK;
	}

	if (ans_res.second == "NOK")
		return net::action_status::START_NOK;

	if (ans_res.second == "ERR")
		return net::action_status::DEBUG_ERR;

	return net::action_status::UNK_STATUS;
}

static net::action_status read_file(net::stream<net::tcp_source>& ans_strm, net::field& name, net::field& file) {
	auto fld = ans_strm.read(1, 24);
	if (fld.first != net::action_status::OK)
		return fld.first;
	name = std::move(fld.second);

	fld = ans_strm.read(1, 4);
	if (fld.first != net::action_status::OK)
		return fld.first;
	size_t fsize = 0;
	try {
		fsize = std::stoul(fld.second.c_str());
	} catch (std::invalid_argument&) {
		return net::action_status::BAD_ARG;
	} catch (std::out_of_range&) {
		return net::action_status::BAD_ARG;
	}

	fld = ans_strm.read(fsize, fsize, false);
	if (fld.first != net::action_status::OK)
		return fld.first;
	file = std::move(fld.second);
	return net::action_status::OK;
}

static net::action_status write_file(const std::string& filename, const std::string& file) {
	std::ofstream stream;
	stream.open(filename, std::ofstream::out | std::ofstream::trunc);
	if (!stream.is_open())
		return net::action_status::PERSIST_ERR;
	stream << file;
	stream.close();
	return net::action_status::OK;
}

static net::action_status do_show_trials(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	auto res = msg.no_more_fields();
	if (res != net::action_status::OK)
		return res;
	if (!in_game)
		return net::action_status::NOT_IN_GAME;
		
	net::out_stream out_strm;
	out_strm.write("STR").write(current_plid).prime();
	std::cout << "Sent buffer: \"" << out_strm.view() << '\"' << std::endl;

	net::tcp_connection tcp{tcp_addr};
	if (!tcp.valid())
		return net::action_status::CONN_ERR;
	auto [ans_ok, ans_strm] = tcp.request(out_strm);
	if (ans_ok != net::action_status::OK)
		return ans_ok;

	auto fld = ans_strm.read(3, 3);
	if (fld.first != net::action_status::OK)
		return fld.first;
	if (fld.second != "RST") {
		if (fld.second == "ERR")
			return net::action_status::RET_ERR;
		return net::action_status::UNK_REPLY;
	}
	fld = ans_strm.read(3, 3);
	if (fld.first != net::action_status::OK)
		return fld.first;
	if (fld.second == "NOK") {
		if ((fld.first = ans_strm.check_strict_end()) != net::action_status::OK)
			return fld.first;
		std::cout << "The specified user has no recorded games (or a problem may have occured)" << std::endl;
		return net::action_status::OK;
	}
	if (fld.second != "FIN" && fld.second != "ACT")
		return net::action_status::UNK_STATUS;
	net::field fname, file;
	fld.first = read_file(ans_strm, fname, file);
	if (fld.first != net::action_status::OK || (fld.first = ans_strm.check_strict_end()) != net::action_status::OK)
		return fld.first;
	write_file(fname, file);
	std::cout << file << std::endl;
	return net::action_status::OK;

}

static net::action_status do_scoreboard(net::stream<net::file_source>& msg, net::udp_connection& udp, const net::self_address& tcp_addr) {
	auto res = msg.no_more_fields();
	if (res != net::action_status::OK)
		return res;
		
	net::out_stream out_strm;
	out_strm.write("SSB").prime();
	std::cout << "Sent buffer: \"" << out_strm.view() << '\"' << std::endl;

	net::tcp_connection tcp{tcp_addr};
	if (!tcp.valid())
		return net::action_status::CONN_ERR;
	auto [ans_ok, ans_strm] = tcp.request(out_strm);
	if (ans_ok != net::action_status::OK)
		return ans_ok;
	auto fld = ans_strm.read(3, 3);
	if (fld.first != net::action_status::OK)
		return fld.first;
	if (fld.second != "RSS") {
		if (fld.second == "ERR")
			return net::action_status::RET_ERR;
		return net::action_status::UNK_REPLY;
	}
	fld = ans_strm.read(2, 5);
	if (fld.first != net::action_status::OK)
		return fld.first;
	if (fld.second == "EMPTY") {
		if ((fld.first = ans_strm.check_strict_end()) != net::action_status::OK)
			return fld.first;
		std::cout << "The scoreboard is empty." << std::endl;
		return net::action_status::OK;
	}
	if (fld.second != "OK")
		return net::action_status::UNK_STATUS;
	net::field fname, file;
	size_t fsize = 0;
	fld.first = read_file(ans_strm, fname, file);
	if (fld.first != net::action_status::OK || (fld.first = ans_strm.check_strict_end()) != net::action_status::OK)
		return fld.first;
	write_file(fname, file);
	std::cout << file << std::endl;
	return net::action_status::OK;
}
