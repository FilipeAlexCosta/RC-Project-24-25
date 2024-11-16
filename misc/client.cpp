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
	actions.add_action("start",
		[](const net::message& msg) -> int {
			std::cout << "Inside start action\n";
			for (auto f : msg)
				std::cout << "Field: \"" << f << "\"\n";
			return 0;
		}
	);

	actions.add_action("try",
		[](const net::message& msg) -> int {
			std::cout << "Inside try action\n";
			for (auto f : msg)
				std::cout << "Field: \"" << f << "\"\n";
			return 0;
		}
	);

	actions.add_action("show_trials",
		[](const net::message& msg) -> int {
			std::cout << "Inside show_trials action\n";
			for (auto f : msg)
				std::cout << "Field: \"" << f << "\"\n";
			return 0;
		}
	);

	actions.add_action("st",
		[](const net::message& msg) -> int {
			std::cout << "Inside st action\n";
			for (auto f : msg)
				std::cout << "Field: \"" << f << "\"\n";
			return 0;
		}
	);

	while (true) {
		std::string input;
		std::getline(std::cin, input);
		if (actions.execute(input))
			std::cout << "Failed to execute an action\n";
	}
	actions.execute("start test");
	return 0;
}
