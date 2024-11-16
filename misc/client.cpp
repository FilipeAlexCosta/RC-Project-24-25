#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>

#include "common.hpp"
#include <iostream>

#define PORT "58001"

void test(const std::string& str) {
	net::message msg{str};
	size_t i = 1;
	while (msg.has_next()) {
		size_t j = 1;
		std::cout << "||| Field " << i << "|||\n";
		std::cout << "Is in del phase? " << msg.is_in_delimiter_phase() << "\n";
		for (auto f = msg.rbegin(); f != msg.rend(); f++) {
			std::cout << j << ": " << (*f) << "\n";
			j++;
		}
		/*for (char c : msg) {
			std::cout << j << ": " << c << "\n";
			j++;
		}*/
		msg.next_field();
		i++;
	};
}

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

	test("start PLID max_playtime");
	test(" start PLID max_playtime ");
	test("");
	return 0;
}
