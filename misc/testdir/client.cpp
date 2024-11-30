#include "common.hpp"
#include <iostream>
#include <cstring>

#define PORT "58011"
#define TIMEOUT 5

static bool in_game = false;
static bool exit_app = false;
static char current_plid[PLID_SIZE];
static char current_trial = '0';

static net::action_status do_start(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info);
static net::action_status do_try(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info);
static net::action_status do_show_trials(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info);
static net::action_status do_scoreboard(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info);
static net::action_status do_quit(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info);
static net::action_status do_exit(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info);
static net::action_status do_debug(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info);

int main() {
	net::socket_context udp_info{"tejo.tecnico.ulisboa.pt", PORT, SOCK_DGRAM};
	if (!udp_info.is_valid()) {
		std::cout << "Failed to create udp socket. Check if the provided address and port are correct.\n";
		return 1;
	}
	if (udp_info.set_timeout(5)) {
		std::cout << "Failed to set udp socket's timeout.\n"; 
		return 1;
	}

	net::socket_context tcp_info{"tejo.tecnico.ulisboa.pt", PORT, SOCK_STREAM};
	if (!tcp_info.is_valid()) {
		std::cout << "Failed to create tcp socket. Check if the provided address and port are correct.\n";
		return 1;
	}

	net::action_map<net::file_source, net::socket_context&, net::socket_context&> actions;
	actions.add_action("start", do_start);
	actions.add_action("try", do_try);
	/*actions.add_action({"show_trials", "st"}, do_show_trials);
	actions.add_action({"scoreboard", "sb"}, do_scoreboard);*/
	actions.add_action("quit", do_quit);
	actions.add_action("exit", do_exit);
	/*actions.add_action("debug", do_debug);*/

	while (!exit_app) {
		net::stream<net::file_source> strm{STDIN_FILENO, false};
		auto status = actions.execute(strm, udp_info, tcp_info);
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

static net::action_status do_start(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info) {
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

	char ans_buf[UDP_MSG_SIZE];
	int ans_bytes = -1;
	status = net::udp_request(out_strm, udp_info, ans_buf, UDP_MSG_SIZE, ans_bytes);
	if (status != net::action_status::OK)
		return status;

	std::cout << "Received buffer: \"" << std::string_view{ans_buf, static_cast<size_t>(ans_bytes)} << '\"' << std::endl;

	net::stream<net::udp_source> ans_strm{std::string_view{ans_buf, static_cast<size_t>(ans_bytes)}};
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

static net::action_status do_try(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info) {
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

	char ans_buf[UDP_MSG_SIZE];
	int ans_bytes = -1;
	res.first = net::udp_request(out_strm, udp_info, ans_buf, UDP_MSG_SIZE, ans_bytes);
	if (res.first != net::action_status::OK)
		return res.first;
	
	std::cout << "Received buffer: \"" << std::string_view{ans_buf, static_cast<size_t>(ans_bytes)} << "\"" << std::endl;

	net::stream<net::udp_source> ans_strm{std::string_view{ans_buf, static_cast<size_t>(ans_bytes)}};
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
	if (res.second == "INV") // TODO: close down or???
		return net::action_status::TRY_INV;
	if (res.second == "NOK")
		return net::action_status::TRY_NOK;
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

static net::action_status end_game(net::socket_context& udp_info) {
	net::out_stream out_strm;
	out_strm.write("QUT").write(current_plid).prime();

	char ans_buf[UDP_MSG_SIZE];
	int ans_bytes = -1;
	auto status = net::udp_request(out_strm, udp_info, ans_buf, UDP_MSG_SIZE, ans_bytes);
	if (status != net::action_status::OK)
		return status;

	std::cout << "Received buffer: \"" << std::string_view{ans_buf, static_cast<size_t>(ans_bytes)} << '\"' << std::endl;
	net::stream<net::udp_source> ans_strm{std::string_view{ans_buf, static_cast<size_t>(ans_bytes)}};

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

	if (res.second == "NOK")
		return net::action_status::NOT_IN_GAME;

	if (res.second != "OK")
		return net::action_status::UNK_STATUS;

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
	std::cout << "You quit the game! The secret key was ";
	std::cout << correct_guess << '.' << std::endl;
	return net::action_status::OK;
}


static net::action_status do_quit(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info) {
	auto res = msg.no_more_fields();
	if (res != net::action_status::OK)
		return res;

	if (!in_game)
		return net::action_status::NOT_IN_GAME;

	return end_game(udp_info);
}

static net::action_status do_exit(net::stream<net::file_source>& msg, net::socket_context& udp_info, net::socket_context& tcp_info) {
	auto res = msg.no_more_fields();
	if (res != net::action_status::OK)
		return res;

	if (!in_game) {
		exit_app = true;
		return net::action_status::OK;
	}

	exit_app = ((res = end_game(udp_info)) == net::action_status::OK);
	return res;
}

/*static net::action_status do_try(const std::string& msg, net::socket_context& udp_info, net::socket_context& tcp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1, 1, 1, 1, 1});
	if (status != net::action_status::OK)
		return status;

	for (size_t i = 1; i < fields.size(); i++) {
		status = net::is_valid_color(fields[i]);
		if (status != net::action_status::OK)
			return status;
	}

	if (!in_game)
		return net::action_status::NOT_IN_GAME;
	current_trial++;

	fields[0] = "TRY";
	fields.insert(std::begin(fields) + 1, current_plid);
	fields.push_back(net::field(&current_trial, 1));

	char req_buf[UDP_MSG_SIZE];
	int n_bytes = net::prepare_buffer(req_buf, (sizeof(req_buf) / sizeof(char)), fields);

	//std::cout << "Sent buffer: \"" << req_buf << '\"';

	char ans_buf[UDP_MSG_SIZE];
	int ans_bytes = -1;
	status = net::udp_request(req_buf, n_bytes, udp_info, ans_buf, UDP_MSG_SIZE, ans_bytes);
	if (status != net::action_status::OK)
		return status;
	
	//std::cout << "Received buffer: \"" << ans_buf;

	auto res = net::get_fields_strict(ans_buf, ans_bytes, {3});
	if (res.first == net::action_status::OK) { // checking for ERR
		if (res.second[0] == "ERR")
			return net::action_status::RET_ERR;
		return net::action_status::UNK_REPLY;
	}
	
	res = net::get_fields_strict(ans_buf, ans_bytes, {3, 3});
	if (res.first == net::action_status::OK) { // check for DUP, INV, NOK, ERR
		if (res.second[0] != "RTR")
			return net::action_status::UNK_REPLY;
		if (res.second[1] == "DUP") {
			current_trial--;
			return net::action_status::TRY_DUP;
		}
		if (res.second[1] == "INV") // TODO: close down or???
			return net::action_status::TRY_INV;
		if (res.second[1] == "NOK")
			return net::action_status::TRY_NOK;
		if (res.second[1] == "ERR")
			return net::action_status::TRY_ERR;
		return net::action_status::UNK_STATUS;
	}

	res = net::get_fields_strict(ans_buf, ans_bytes, {3, 3, 1, 1, 1, 1}); // check for ENT & ETM
	if (res.first == net::action_status::OK) {
		if (res.second[0] != "RTR")
			return net::action_status::UNK_REPLY;
		bool is_ENT = res.second[1] == "ENT";
		if (is_ENT || res.second[1] == "ETM") {
			for (size_t i = 2; i < res.second.size(); i++) {
				auto status = net::is_valid_color(res.second[i]);
				if (status != net::action_status::OK)
					return status;
			}
			if (is_ENT)
				std::cout << "You ran out of tries!";
			else
				std::cout << "You ran out of time!";
			for (size_t i = 2; i < res.second.size(); i++)
				std::cout << res.second[i] << ' ';
			std::cout << "was the correct guess!\n";
			in_game = false;
			return net::action_status::OK;
		}
		return net::action_status::UNK_STATUS;
	}

	res = net::get_fields_strict(ans_buf, ans_bytes, {3, 2, 1, 1, 1}); // check for OK
	if (res.first == net::action_status::OK) {
		if (res.second[0] != "RTR")
			return net::action_status::UNK_REPLY;
		if (res.second[1] != "OK")
			return net::action_status::UNK_STATUS;
		if (res.second[2][0] != current_trial) {
			if (res.second[2][0] == (current_trial - 1))
				current_trial--;
			else
				return net::action_status::TRY_INV;
		}
		if (res.second[3][0] < '0' || res.second[4][0] < '0')
			return net::action_status::TRY_NT; // TODO err message
		if (res.second[3][0] + res.second[4][0] - 2 * '0' > '4') // nB + nW <= 4
			return net::action_status::TRY_NT; // TODO err message

		if (res.second[3][0] == '4') { // check for a win
			in_game = false;
			std::cout << "You won in " << current_trial << " tries!\n";
			return net::action_status::OK;
		}
		std::cout << "You guessed " << res.second[3] << " colors in the correct place and";
		std::cout << " there were " << res.second[4] << " correct colors with an incorrect placement\n";
		return net::action_status::OK;
	}
	return net::action_status::UNK_STATUS;
}

static net::action_status do_show_trials(const std::string& msg, net::socket_context& udp_info, net::socket_context& tcp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1});
	if (status != net::action_status::OK)
		return status;
	if (!in_game)
		return net::action_status::NOT_IN_GAME;
		
	fields[0] = "STR";
	fields.push_back(current_plid);

	char req_buf[UDP_MSG_SIZE];
	int n_bytes = net::prepare_buffer(req_buf, (sizeof(req_buf) / sizeof(char)), fields);

	std::cout << "Sent buffer: \"" << req_buf << '\"';

	char ans_buf[UDP_MSG_SIZE];
	int ans_bytes = -1;
	status = net::tcp_request(req_buf, n_bytes, tcp_info, ans_buf, 4, ans_bytes);
	if (status != net::action_status::OK)
		return status;
	
	std::cout << "Received buffer: \"" << ans_buf;
	return net::action_status::OK;
}

static net::action_status do_scoreboard(const std::string& msg, net::socket_context& udp_info, net::socket_context& tcp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1});
	if (status != net::action_status::OK)
		return status;

	fields[0] = "SSB";

	char req_buf[UDP_MSG_SIZE];
	int n_bytes = net::prepare_buffer(req_buf, (sizeof(req_buf) / sizeof(char)), fields);

	std::cout << "Sent buffer: \"" << req_buf << '\"';

	char ans_buf[UDP_MSG_SIZE];
	int ans_bytes = -1;
	status = net::udp_request(req_buf, n_bytes, tcp_info, ans_buf, UDP_MSG_SIZE, ans_bytes);
	std::cout << "Received buffer: \"" << ans_buf;
	if (status != net::action_status::OK)
		return status;
	
	//std::cout << "Received buffer: \"" << ans_buf;
	return net::action_status::OK;
}

static net::action_status do_debug(const std::string& msg, net::socket_context& udp_info, net::socket_context& tcp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1, PLID_SIZE, -1, 1, 1, 1, 1});
	if (status != net::action_status::OK)
		return status;

	status = net::is_valid_plid(fields[1]);
	if (status != net::action_status::OK)
		return status;

	status = net::is_valid_max_playtime(fields[2]);
	if (status != net::action_status::OK)
		return status;

	for (size_t i = 3; i < fields.size(); i++) {
		status = net::is_valid_color(fields[i]);
		if (status != net::action_status::OK)
			return status;
	}

	if (in_game)
		return net::action_status::ONGOING_GAME;

	char sent_m_time[MAX_PLAYTIME_SIZE];
	net::fill_max_playtime(sent_m_time, fields[2]);
	fields[0] = "DBG";
	fields[2] = net::field(sent_m_time, MAX_PLAYTIME_SIZE);

	char req_buf[UDP_MSG_SIZE];
	int n_bytes = net::prepare_buffer(req_buf, (sizeof(req_buf) / sizeof(char)), fields);

	//std::cout << "Sent buffer: \"" << req_buf << '\"';

	char ans_buf[UDP_MSG_SIZE];
	int ans_bytes = -1;
	status = net::udp_request(req_buf, n_bytes, udp_info, ans_buf, UDP_MSG_SIZE, ans_bytes);
	if (status != net::action_status::OK)
		return status;
	
	//std::cout << "Received buffer: \"" << ans_buf;

	auto res = net::get_fields_strict(ans_buf, ans_bytes, {3});
	if (res.first == net::action_status::OK) { // checking for ERR
		if (res.second[0] == "ERR")
			return net::action_status::RET_ERR;
		return net::action_status::UNK_REPLY;
	}
	res = net::get_fields_strict(ans_buf, ans_bytes, {3, -1});
	if (res.first != net::action_status::OK)
		return res.first;

	if (res.second[0] != "RDB")
		return net::action_status::UNK_REPLY;

	if (res.second[1] == "NOK")
		return net::action_status::START_NOK;

	if (res.second[1] == "ERR")
		return net::action_status::DEBUG_ERR;

	if (res.second[1] == "OK") {
		setup_game_clientside(fields[1]);
		return net::action_status::OK;
	}

	return net::action_status::UNK_STATUS;
}*/
