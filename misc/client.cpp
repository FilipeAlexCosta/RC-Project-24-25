#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "common.hpp"
#include <iostream>

#define PORT "58001"

static net::action_status do_start(const net::message& msg);

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

	/*actions.add_action("try",
		[](const net::message& msg) -> int {
			std::cout << "Inside try action\n";
			for (auto f : msg)
				std::cout << "Field: \"" << f << "\"\n";
			return 0;
		}
	);

	actions.add_action({"show_trials", "st"},
		[](const net::message& msg) -> int {
			std::cout << "Inside show_trials action\n";
			for (auto f : msg)
				std::cout << "Field: \"" << f << "\"\n";
			return 0;
		}
	);

	actions.add_action({"scoreboard", "sb"},
		[](const net::message& msg) -> int {
			std::cout << "Inside scoreboard action\n";
			for (auto f : msg)
				std::cout << "Field: \"" << f << "\"\n";
			return 0;
		}
	);

	actions.add_action("quit",
		[](const net::message& msg) -> int {
			std::cout << "Inside quit action\n";
			for (auto f : msg)
				std::cout << "Field: \"" << f << "\"\n";
			return 0;
		}
	);

	actions.add_action("exit",
		[](const net::message& msg) -> int {
			std::cout << "Inside debug action\n";
			for (auto f : msg)
				std::cout << "Field: \"" << f << "\"\n";
			return 0;
		}
	);

	actions.add_action("debug",
		[](const net::message& msg) -> int {
			std::cout << "Inside debug action\n";
			for (auto f : msg)
				std::cout << "Field: \"" << f << "\"\n";
			return 0;
		}
	);*/

	while (true) {
		std::string input;
		std::getline(std::cin, input);
		if (actions.execute(input) != net::action_status::OK)
			std::cout << "Failed to execute an action\n";
	}
	actions.execute("start test");
	return 0;
}

static net::action_status do_start(const net::message& msg) {
	auto field_it = std::begin(msg);
	if (field_it.is_in_delimiter_phase()) // ignore leading whitespace
		++field_it;
	if ((++field_it) == std::end(msg)) // ignore "start"
		return net::action_status::MISSING_ARG;
	if ((++field_it) == std::end(msg)) // ignore delimiter phase
		return net::action_status::MISSING_ARG;

	int plid = -1;
	if ((*field_it).length() > 6) // PLID has 6 digits
		return net::action_status::BAD_ARG;
	try {
		plid = std::stoi(std::string(*field_it));
	} catch (const std::invalid_argument& err) { // cannot be read
		return net::action_status::BAD_ARG;
	} // out_of_range exception shouldn't be an issue
	if (plid < 0) // plid must be non-negative
		return net::action_status::BAD_ARG;

	if ((++field_it) == std::end(msg)) // go to del phase
		return net::action_status::MISSING_ARG;
	if ((++field_it) == std::end(msg)) // go to max_playtime
		return net::action_status::MISSING_ARG;

	int max_playtime = -1;
	if ((*field_it).length() > 3) // check <= 999
		return net::action_status::BAD_ARG;
	try {
		max_playtime = std::stoi(std::string(*field_it));
	} catch (const std::invalid_argument& err) { // cannot be read
		return net::action_status::BAD_ARG;
	} // out_of_range exception shouldn't be an issue
	if (max_playtime < 0 || max_playtime > 600) // check <= 600
		return net::action_status::BAD_ARG;

	if ((++field_it) == std::end(msg) || (++field_it) == std::end(msg))
		return net::action_status::OK;
	return net::action_status::EXCESS_ARGS;
}
