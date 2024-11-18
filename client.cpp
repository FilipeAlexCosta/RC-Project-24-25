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

#define PORT "58001"

static bool in_game = false;
static bool exit_app = false;
static char current_plid[6];
static const std::string_view valid_colors = "RGBYOP";

static bool is_valid_color(char color);

static net::action_status do_start(const net::message& msg);
static net::action_status do_try(const net::message& msg);
static net::action_status do_show_trials(const net::message& msg);
static net::action_status do_scoreboard(const net::message& msg);
static net::action_status do_quit(const net::message& msg);
static net::action_status do_exit(const net::message& msg);
static net::action_status do_debug(const net::message& msg);

int main() {
	/*int fd, errcode;
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

	if ((errcode = getaddrinfo("localhost", PORT, &hints, &res)) != 0)
		return 1;

	if ((n = sendto(fd, "Hello!\n", 7, 0, res->ai_addr, res->ai_addrlen)) == -1)
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
		if (actions.execute(input) != net::action_status::OK)
			std::cout << "Failed to execute an action\n";
		if (exit_app)
			return 0; // TODO: maybe do the actual closing down
	}
	actions.execute("start test");
	return 0;
}

static bool is_valid_color(char color) {
	for (auto v_color : valid_colors)
		if (color == v_color)
			return true;
	return false;
}

// Read PLID
static net::action_status parse_plid(const std::string& field) {
	if (field.length() != 6) // PLID has 6 digits
		return net::action_status::BAD_ARG;
	try {
		std::strcpy(current_plid, field.c_str());
	} catch (const std::invalid_argument& err) { // cannot be read
		return net::action_status::BAD_ARG;
	} // out_of_range exception shouldn't be an issue
	if (std::stoi(current_plid) < 0) // plid must be non-negative
		return net::action_status::BAD_ARG;
	return net::action_status::OK;
}

static net::action_status parse_max_playtime(const std::string& field, int& max_playtime) {
	// Is this check needed?
	// if (field.length() > 3) // check <= 999 
	// 	return net::action_status::BAD_ARG;

	try {
		max_playtime = std::stoi(std::string(field));
	} catch (const std::invalid_argument& err) { // cannot be read
		return net::action_status::BAD_ARG;
	} // out_of_range exception shouldn't be an issue
	if (max_playtime < 0 || max_playtime > 600) // check <= 600
		return net::action_status::BAD_ARG;
	return net::action_status::OK;
}

static net::action_status parse_guess(const net::message& msg, char guess[4]) {
	auto field_it = std::begin(msg);
	for (size_t i = 0; i < 3; i++) { 
		auto field = *field_it;
		guess[i] = field[0];
		if (field.length() != 1 || !is_valid_color(field[0]))
			return net::action_status::BAD_ARG;
		if ((++field_it) == std::end(msg)) // go to del phase
			return net::action_status::MISSING_ARG;
		if ((++field_it) == std::end(msg)) // go to next color
			return net::action_status::MISSING_ARG;
	}

	auto field = *field_it;
	guess[3] = field[0];
	if (field.length() != 1 || !is_valid_color(field[0]))
		return net::action_status::BAD_ARG;
	return net::action_status::OK;
}

static net::action_status do_start(const net::message& msg) {
	auto field_it = std::begin(msg);
	if (field_it.is_in_delimiter_phase()) // ignore leading whitespace
		++field_it;
	if ((++field_it) == std::end(msg)) // ignore "start"
		return net::action_status::MISSING_ARG;
	if ((++field_it) == std::end(msg)) // ignore delimiter phase
		return net::action_status::MISSING_ARG;

	auto status = parse_plid(std::string(*field_it));
	if (status != net::action_status::OK)
		return status;

	if ((++field_it) == std::end(msg)) // go to del phase
		return net::action_status::MISSING_ARG;
	if ((++field_it) == std::end(msg)) // go to max_playtime
		return net::action_status::MISSING_ARG;

	int max_playtime = -1;
	status = parse_max_playtime(std::string(*field_it), max_playtime);
	if (status != net::action_status::OK)
		return status;

	if ((++field_it) != std::end(msg) && (++field_it) != std::end(msg))
		return net::action_status::EXCESS_ARGS;
	if (in_game)
		return net::action_status::ONGOING_GAME;
	in_game = true;

	std::cout << "PLID: " << current_plid << '\n';
	std::cout << "time: " << max_playtime << '\n';

	return net::action_status::OK;
}

static net::action_status do_try(const net::message& msg) {
	auto field_it = std::begin(msg);
	if (field_it.is_in_delimiter_phase()) // ignore leading whitespace
		++field_it;
	if ((++field_it) == std::end(msg)) // ignore "try"
		return net::action_status::MISSING_ARG;
	if ((++field_it) == std::end(msg)) // ignore delimiter phase
		return net::action_status::MISSING_ARG;

	char guess[4];	
	auto status = parse_guess(msg, guess);
	if (status != net::action_status::OK)
		return status;

	if ((++field_it) != std::end(msg) && (++field_it) != std::end(msg))
		return net::action_status::EXCESS_ARGS;
	if (!in_game)
		return net::action_status::NOT_IN_GAME;

	std::cout << "Guess: " << guess << '\n';
	return net::action_status::OK;
}

static net::action_status do_show_trials(const net::message& msg) {
	auto field_it = std::begin(msg);
	if (field_it.is_in_delimiter_phase()) // ignore leading whitespace
		++field_it;
	if ((++field_it) != std::end(msg) && (++field_it) != std::end(msg))
		return net::action_status::EXCESS_ARGS; // ignores keyword and trailing ws
	if (!in_game)
		return net::action_status::NOT_IN_GAME;
	
	// use PLID ...
	
	return net::action_status::OK;
}

static net::action_status do_scoreboard(const net::message& msg) {
	auto field_it = std::begin(msg);
	if (field_it.is_in_delimiter_phase()) // ignore leading whitespace
		++field_it;
	if ((++field_it) != std::end(msg) && (++field_it) != std::end(msg))
		return net::action_status::EXCESS_ARGS; // ignores keyword and trailing ws
	
	return net::action_status::OK;
}

static net::action_status do_quit(const net::message& msg) {
	auto field_it = std::begin(msg);
	if (field_it.is_in_delimiter_phase()) // ignore leading whitespace
		++field_it;
	if ((++field_it) != std::end(msg) && (++field_it) != std::end(msg))
		return net::action_status::EXCESS_ARGS; // ignores keyword and trailing ws
	in_game = false;
	return net::action_status::OK;
}

static net::action_status do_exit(const net::message& msg) {
	auto field_it = std::begin(msg);
	if (field_it.is_in_delimiter_phase()) // ignore leading whitespace
		++field_it;
	if ((++field_it) != std::end(msg) && (++field_it) != std::end(msg))
		return net::action_status::EXCESS_ARGS; // ignores keyword and trailing ws
	in_game = false;
	exit_app = true;
	return net::action_status::OK;
}

static net::action_status do_debug(const net::message& msg) {
	auto field_it = std::begin(msg);
	if (field_it.is_in_delimiter_phase()) // ignore leading whitespace
		++field_it;
	if ((++field_it) == std::end(msg)) // ignore "debug"
		return net::action_status::MISSING_ARG;
	if ((++field_it) == std::end(msg)) // ignore delimiter phase
		return net::action_status::MISSING_ARG;	
	auto status = parse_plid(std::string(*field_it));
	if (status != net::action_status::OK)
		return status;

	if ((++field_it) == std::end(msg)) // go to del phase
		return net::action_status::MISSING_ARG;
	if ((++field_it) == std::end(msg)) // go to max_playtime
		return net::action_status::MISSING_ARG;

	int max_playtime = -1;
	status = parse_max_playtime(std::string(*field_it), max_playtime);
	if (status != net::action_status::OK)
		return status;
	
	if ((++field_it) == std::end(msg)) // go to del phase
		return net::action_status::MISSING_ARG;
	if ((++field_it) == std::end(msg)) // go to 1st guess
		return net::action_status::MISSING_ARG;
	
	char guess[4];
	status = parse_guess(msg, guess);
	if (status != net::action_status::OK)
		return status;

	if ((++field_it) != std::end(msg) && (++field_it) != std::end(msg))
		return net::action_status::EXCESS_ARGS;

	if (in_game)
		return net::action_status::ONGOING_GAME;
	in_game = true;

	std::cout << "PLID: " << current_plid << '\n';
	std::cout << "time: " << max_playtime << '\n';
	std::cout << "Guess: " << guess << '\n';
	
	return net::action_status::OK;
}

