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
	
	res = net::get_fields_strict(ans_buf, ans_bytes, {3, -1});

	if (res.second[0] != "RTR")
		return net::action_status::UNK_REPLY;

	if (res.second[1] == "DUP") {
		current_trial--;
		return net::action_status::TRY_DUP;
	}
	
	if (res.second[1] == "INV") {
		// TODO: fix
		return net::action_status::TRY_INV;
	}

	if (res.second[1] == "NOK")
		return net::action_status::TRY_NOK;
	
	if (res.second[1] == "ENT") {
		if (current_trial != MAX_TRIALS) { //TODO: fix
			current_trial = MAX_TRIALS;
			return net::action_status::TRY_NT;
		}
		res = net::get_fields_strict(ans_buf, ans_bytes, {3, -1, 1, 1, 1, 1});
		if (res.first != net::action_status::OK)
			return res.first;
		std::string key;
		for (size_t i = 2; i < res.second.size(); i++) { // checking if received secret key is valid
			status = net::is_valid_color(res.second[i]);
			if (status != net::action_status::OK)
				return status;
			if (!key.empty())
				key += ' ';
			key += res.second[i];
		}
		std::cout << "Secret key: " << key << '\n'; //TODO: fix order of messages
		in_game = false;
		return net::action_status::TRY_ENT;
	}
	
	if (res.second[1] == "ETM") {
		res = net::get_fields_strict(ans_buf, ans_bytes, {3, -1, 1, 1, 1, 1});
		if (res.first != net::action_status::OK)
			return res.first;
		std::string key;
		for (size_t i = 2; i < res.second.size(); i++) { // checking if received secret key is valid
			status = net::is_valid_color(res.second[i]);
			if (status != net::action_status::OK)
				return status;
			if (!key.empty())
				key += ' ';
			key += res.second[i];
		}
		std::cout << "Secret key: " << key << '\n'; //TODO: fix order of messages
		in_game = false;
		return net::action_status::TRY_ETM;
	}

	if (res.second[1] == "ERR")
		return net::action_status::TRY_ERR;

	if (res.second[1] == "OK") {
		res = net::get_fields_strict(ans_buf, ans_bytes, {3, -1, 1, 1, 1});
		if (res.first != net::action_status::OK)
			return res.first;
		char nT = res.second[2][0], nB = res.second[3][0], nW = res.second[4][0];
		if (nT != current_trial) { //TODO: fix
			if (nT == current_trial - 1) // resend case: server recognized it was a resent trial (...)
				current_trial--;
			else { //TODO: Resynchronize might not be correct
				current_trial = nT; //Resynchronize
				return net::action_status::TRY_NT;
			}
		}
		if (nB == '4') {
			in_game = false;
			std::cout << "Game Won! The secret key was guessed correctly\n";
		} else
			std::cout << "Trial " << nT << ": " << nB << " guesses correct in both color and position, " << nW << " guesses correct in color but incorrectly positioned.\n";
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

static net::action_status do_quit(const std::string& msg, net::socket_context& udp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1});
	if (status != net::action_status::OK)
		return status;

	if (!in_game)
		return net::action_status::NOT_IN_GAME;

	fields[0] = "QUT";
	fields.push_back(current_plid);

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

	if (res.second[0] != "RQT")
		return net::action_status::UNK_REPLY;
	if (res.second[1] == "ERR")
		return net::action_status::QUIT_EXIT_ERR;

	if (res.second[1] == "NOK") 
		return net::action_status::NOT_IN_GAME; // can't receive this from server (...)
	
	if (res.second[1] == "OK") {
		res = net::get_fields_strict(ans_buf, ans_bytes, {3, -1, 1, 1, 1, 1});
		if (res.first != net::action_status::OK)
			return res.first;
		std::string key;
		for (size_t i = 2; i < res.second.size(); i++) { // checking if received secret key is valid
			status = net::is_valid_color(res.second[i]);
			if (status != net::action_status::OK)
				return status;
			if (!key.empty())
				key += ' ';
			key += res.second[i];
		}
		std::cout << "Server terminated a game with the given secret key: " << key << '\n';

		in_game = false;
		return net::action_status::OK;
	}

	return net::action_status::UNK_STATUS;
}

static net::action_status do_exit(const std::string& msg, net::socket_context& udp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1});
	if (status != net::action_status::OK)
		return status;

	if (!in_game) {
		exit_app = true;
		return net::action_status::OK;
	}

	fields[0] = "QUT";
	fields.push_back(current_plid);

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

	if (res.second[0] != "RQT")
		return net::action_status::UNK_REPLY;
	if (res.second[1] == "ERR")
		return net::action_status::QUIT_EXIT_ERR;
	
	if (res.second[1] == "OK" || res.second[1] == "NOK") {
		if (res.second[1] == "OK") {
			res = net::get_fields_strict(ans_buf, ans_bytes, {3, -1, 1, 1, 1, 1});
			if (res.first != net::action_status::OK)
				return res.first;
			std::string key;
			for (size_t i = 2; i < res.second.size(); i++) { // checking if received secret key is valid
				status = net::is_valid_color(res.second[i]);
				if (status != net::action_status::OK)
					return status;
				if (!key.empty())
					key += ' ';
				key += res.second[i];
			}
			std::cout << "Server terminated a game with the given secret key: " << key << '\n';
			in_game = false;
		}

		exit_app = true;
		return net::action_status::OK;
	}

	return net::action_status::UNK_STATUS;
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