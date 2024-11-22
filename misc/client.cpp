#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

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
	ssize_t n;
	struct addrinfo hints, *res;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	char buffer[128];
	char host[NI_MAXHOST], service[NI_MAXSERV];

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		return 1;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if ((errcode = getaddrinfo("tejo.tecnico.ulisboa.pt", PORT, &hints, &res)) != 0)
		return 1;

	net::socket_context udp_info{
		fd,
		res,
		&addr,
		&addrlen
	};

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
		if (exit_app)
			return 0; // TODO: maybe do the actual closing down
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

	// TODO: transform max_playtime into 3 digits
	char buffer[UDP_MSG_SIZE];
	int n_bytes = net::prepare_buffer(buffer, (sizeof(buffer) / sizeof(char)), fields);
	/*if (net::message::prepare_buffer(
		buffer,
		UDP_MSG_SIZE,
		DEFAULT_SEP,
		DEFAULT_EOM,
		"SNG", plid, max_playtime 
	) == -1);*/ // TODO: handle error
	// TODO: send request

	std::cout << "Sent buffer: \"" << buffer << "\"\n";

	int n;
	if ((n = sendto(udp_info.socket_fd, buffer, n_bytes, 0, udp_info.receiver_info->ai_addr, udp_info.receiver_info->ai_addrlen)) == -1)
		return net::action_status::ERR;
	if ((n = recvfrom(udp_info.socket_fd, buffer, sizeof(buffer), 0, (struct sockaddr*) udp_info.sender_addr, udp_info.sender_addr_len)) == -1)
		return net::action_status::ERR;

	setup_game_clientside(fields[1]);

	std::cout << "PLID: " << fields[1] << '\n';
	std::cout << "time: " << fields[2] << '\n';
	std::cout << "Received buffer: \"" << buffer << "\"\n";
	return net::action_status::OK;
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


	char buffer[UDP_MSG_SIZE];
	net::prepare_buffer(buffer, (sizeof(buffer) / sizeof(char)), fields);

	std::cout << "Guess: " << fields[1] << fields[2] << fields[3] << fields[4] << '\n';
	std::cout << "Sent buffer: \"" << buffer << "\"\n";
	return net::action_status::OK;
}

static net::action_status do_show_trials(const std::string& msg, net::socket_context& udp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1});
	if (status != net::action_status::OK)
		return status;
	if (!in_game)
		return net::action_status::NOT_IN_GAME;
	
	// use PLID ...

	fields[0] = "STR";
	fields.push_back(current_plid);
	char buffer[UDP_MSG_SIZE];
	net::prepare_buffer(buffer, (sizeof(buffer) / sizeof(char)), fields);

	std::cout << "Sent buffer: \"" << buffer << "\"\n";
	
	return net::action_status::OK;
}

static net::action_status do_scoreboard(const std::string& msg, net::socket_context& udp_info) {
	auto [status, fields] = net::get_fields(msg.data(), msg.size(), {-1});
	if (status != net::action_status::OK)
		return status;

	fields[0] = "SSB";
	char buffer[UDP_MSG_SIZE];
	net::prepare_buffer(buffer, (sizeof(buffer) / sizeof(char)), fields);

	std::cout << "Sent buffer: \"" << buffer << "\"\n";
	
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
	char buffer[UDP_MSG_SIZE];
	int n_bytes = net::prepare_buffer(buffer, (sizeof(buffer) / sizeof(char)), fields);

	std::cout << "Sent buffer: \"" << buffer << "\"\n";

	int n;
	if ((n = sendto(udp_info.socket_fd, buffer, n_bytes, 0, udp_info.receiver_info->ai_addr, udp_info.receiver_info->ai_addrlen)) == -1)
		return net::action_status::ERR;
	if ((n = recvfrom(udp_info.socket_fd, buffer, sizeof(buffer), 0, (struct sockaddr*) udp_info.sender_addr, udp_info.sender_addr_len)) == -1)
		return net::action_status::ERR;

	in_game = false;
	return net::action_status::OK;
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
	char buffer[UDP_MSG_SIZE];
	int n_bytes = net::prepare_buffer(buffer, (sizeof(buffer) / sizeof(char)), fields);

	std::cout << "Sent buffer: \"" << buffer << "\"\n";

	int n;
	if ((n = sendto(udp_info.socket_fd, buffer, n_bytes, 0, udp_info.receiver_info->ai_addr, udp_info.receiver_info->ai_addrlen)) == -1)
		return net::action_status::ERR;
	if ((n = recvfrom(udp_info.socket_fd, buffer, sizeof(buffer), 0, (struct sockaddr*) udp_info.sender_addr, udp_info.sender_addr_len)) == -1)
		return net::action_status::ERR;

	in_game = false;
	exit_app = true;
	return net::action_status::OK;
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
	fields[0] = "SNG";
	fields[2] = net::field(sent_m_time, MAX_PLAYTIME_SIZE);

	char buffer[UDP_MSG_SIZE];
	net::prepare_buffer(buffer, (sizeof(buffer) / sizeof(char)), fields);

	// TODO: send request
	setup_game_clientside(fields[1]);


	std::cout << "PLID: " << fields[1] << '\n';
	std::cout << "time: " << fields[2] << '\n';
	std::cout << "Guess: " << fields[1] << fields[2] << fields[3] << fields[4] << '\n';
	std::cout << "Sent buffer: \"" << buffer << "\"\n";
	
	return net::action_status::OK;
}

