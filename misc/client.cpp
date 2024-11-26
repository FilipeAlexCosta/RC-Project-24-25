#include "common.hpp"
#include <iostream>
#include <cstring>

#define PORT "58011"

static bool in_game = false;
static bool exit_app = false;
static char current_plid[PLID_SIZE];
static char current_trial = '0';

static net::action_status do_start(const std::string& msg, net::socket_context& udp_info);
static net::action_status do_try(const std::string& msg, net::socket_context& udp_info);
static net::action_status do_show_trials(const std::string& msg, net::socket_context& udp_info);
static net::action_status do_scoreboard(const std::string& msg, net::socket_context& udp_info);
static net::action_status do_quit(const std::string& msg, net::socket_context& udp_info);
static net::action_status do_exit(const std::string& msg, net::socket_context& udp_info);
static net::action_status do_debug(const std::string& msg, net::socket_context& udp_info);

int main() {
	int fd, errcode;
	struct addrinfo hints, *res;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		return 1;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	/* testar no msm pc->"127.0.0.1"*/
	if ((errcode = getaddrinfo("tejo.tecnico.ulisboa.pt", PORT, &hints, &res)) != 0)
		return 1;

	net::socket_context udp_info{
		fd,
		res,
		&addr,
		&addrlen
	};

	struct timeval timeout;
	timeout.tv_sec = 5; // 5 s timeout
	timeout.tv_usec = 0;
	if(setsockopt(udp_info.socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1)
		return 1;

	/*if ((n = sendto(fd, "Hello!\n", 7, 0, res->ai_addr, res->ai_addrlen)) == -1)
		return 1;
	if ((n = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr*) &addr, &addrlen)) == -1)
		return 1;

	write(1, "echo: ", 6);
	write(1, buffer, n);

	if ((errcode = getnameinfo((struct sockaddr*) &addr, addrlen, host, sizeof(host), service, sizeof(service), 0)) != 0)
		return 1;
	printf("sent by [%s:%s]\n", host, service);

	freeaddrinfo(res);
	close(fd);*/

	net::action_map actions;
	actions.add_action("start", do_start);
	actions.add_action("try", do_try);
	actions.add_action({"show_trials", "st"}, do_show_trials);
	actions.add_action({"scoreboard", "sb"}, do_scoreboard);
	actions.add_action("quit", do_quit);
	actions.add_action("exit", do_exit);
	actions.add_action("debug", do_debug);

	while (true) {
		std::string input;
		std::getline(std::cin, input);
		auto status = actions.execute(input, udp_info);
		if (status != net::action_status::OK)
			std::cerr << net::status_to_message(status) << ".\n";
		if (exit_app) {
			std::cout << "Exiting the Player application...\n";
			return 0; // TODO: maybe do the actual closing down
		}
	}

	/*freeaddrinfo(udp_info.receiver_info);
	close(udp_info.socket_fd);*/

	return 0;
}

static void setup_game_clientside(const net::field& plid) {
	in_game = true;
	current_trial = '0';
	for (int i = 0; i < PLID_SIZE; i++)
		current_plid[i] = plid[i];
}

static net::action_status do_start(const std::string& msg, net::socket_context& udp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1, PLID_SIZE, -1});
	if (status != net::action_status::OK)
		return status;
	
	status = net::is_valid_plid(fields[1]);
	if (status != net::action_status::OK)
		return status;

	status = net::is_valid_max_playtime(fields[2]);
	if (status != net::action_status::OK)
		return status;

	if (in_game)
		return net::action_status::ONGOING_GAME;

	char sent_m_time[MAX_PLAYTIME_SIZE];
	net::fill_max_playtime(sent_m_time, fields[2]);
	fields[0] = "SNG";
	fields[2] = net::field(sent_m_time, MAX_PLAYTIME_SIZE);

	char req_buf[UDP_MSG_SIZE];
	int n_bytes = net::prepare_buffer(req_buf, (sizeof(req_buf) / sizeof(char)), fields);

	//std::cout << "Sent buffer: \"" << req_buf << '\"';

	char ans_buf[UDP_MSG_SIZE];
	int ans_bytes = -1;
	status = net::udp_request(req_buf, n_bytes, udp_info, ans_buf, UDP_MSG_SIZE, ans_bytes);
	if (status != net::action_status::OK)
		return status;

	auto res = net::get_fields_strict(ans_buf, ans_bytes, {3});
	if (res.first == net::action_status::OK) { // checking for ERR
		if (res.second[0] == "ERR")
			return net::action_status::RET_ERR;
		return net::action_status::UNK_REPLY;
	}
	res = net::get_fields_strict(ans_buf, ans_bytes, {3, -1});
	if (res.first != net::action_status::OK)
		return res.first;

	//std::cout << "Received buffer: \"" << ans_buf;

	if (res.second[0] != "RSG")
		return net::action_status::UNK_REPLY;

	if (res.second[1] == "NOK")
		return net::action_status::START_NOK;

	if (res.second[1] == "ERR")
		return net::action_status::START_ERR;

	if (res.second[1] == "OK") {
		setup_game_clientside(fields[1]);
		return net::action_status::OK;
	}

	return net::action_status::UNK_STATUS;
}

static net::action_status do_try(const std::string& msg, net::socket_context& udp_info) {
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

static net::action_status do_show_trials(const std::string& msg, net::socket_context& udp_info) {
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
	status = net::udp_request(req_buf, n_bytes, udp_info, ans_buf, UDP_MSG_SIZE, ans_bytes);
	if (status != net::action_status::OK)
		return status;
	
	std::cout << "Received buffer: \"" << ans_buf;
	return net::action_status::OK;
}

static net::action_status do_scoreboard(const std::string& msg, net::socket_context& udp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1});
	if (status != net::action_status::OK)
		return status;

	fields[0] = "SSB";

	char req_buf[UDP_MSG_SIZE];
	int n_bytes = net::prepare_buffer(req_buf, (sizeof(req_buf) / sizeof(char)), fields);

	std::cout << "Sent buffer: \"" << req_buf << '\"';

	char ans_buf[UDP_MSG_SIZE];
	int ans_bytes = -1;
	status = net::udp_request(req_buf, n_bytes, udp_info, ans_buf, UDP_MSG_SIZE, ans_bytes);
	if (status != net::action_status::OK)
		return status;
	
	std::cout << "Received buffer: \"" << ans_buf;
	return net::action_status::OK;
}

static net::action_status end_serverside_game(net::message& fields, net::socket_context& udp_info) {
	fields[0] = "QUT";
	fields.push_back(current_plid);

	char req_buf[UDP_MSG_SIZE];
	int n_bytes = net::prepare_buffer(req_buf, (sizeof(req_buf) / sizeof(char)), fields);

	char ans_buf[UDP_MSG_SIZE];
	int ans_bytes = -1;
	auto status = net::udp_request(req_buf, n_bytes, udp_info, ans_buf, UDP_MSG_SIZE, ans_bytes);
	if (status != net::action_status::OK)
		return status;
	
	auto res = net::get_fields_strict(ans_buf, ans_bytes, {3});
	if (res.first == net::action_status::OK) { // checking for ERR
		if (res.second[0] == "ERR")
			return net::action_status::RET_ERR;
		return net::action_status::UNK_REPLY;
	}

	res = net::get_fields_strict(ans_buf, ans_bytes, {3, 3});
	if (res.first == net::action_status::OK) {
		if (res.second[0] != "RQT")
			return net::action_status::UNK_REPLY;
		if (res.second[1] == "ERR")
			return net::action_status::QUIT_EXIT_ERR;
		if (res.second[1] == "NOK") 
			return net::action_status::NOT_IN_GAME;
	}
	
	res = net::get_fields_strict(ans_buf, ans_bytes, {3, 2, 1, 1, 1, 1});
	if (res.first == net::action_status::OK) {
		if (res.second[0] != "RQT")
			return net::action_status::UNK_REPLY;
		for (size_t i = 2; i < res.second.size(); i++) {
			status = net::is_valid_color(res.second[i]);
			if (status != net::action_status::OK)
				return status;
		}
		std::cout << "You quit the game! ";
		for (size_t i = 2; i < res.second.size(); i++)
			std::cout << res.second[i] << ' ';
		std::cout << "was the secret key.\n";
		in_game = false;
		return net::action_status::OK;
	}

	return net::action_status::UNK_STATUS;

}

static net::action_status do_quit(const std::string& msg, net::socket_context& udp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1});
	if (status != net::action_status::OK)
		return status;

	if (!in_game)
		return net::action_status::NOT_IN_GAME;

	return end_serverside_game(fields, udp_info);
}

static net::action_status do_exit(const std::string& msg, net::socket_context& udp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1});
	if (status != net::action_status::OK)
		return status;

	if (!in_game) {
		exit_app = true;
		return net::action_status::OK;
	}

	exit_app = ((status = end_serverside_game(fields, udp_info)) == net::action_status::OK);
	return status;
}

static net::action_status do_debug(const std::string& msg, net::socket_context& udp_info) {
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
}
