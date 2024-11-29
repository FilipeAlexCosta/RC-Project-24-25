#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include "common.hpp"
#include <iostream>

#define PORT "58000"

/* signal(SIGPIPE, SIG_IGN)
 * signal(SIGCHILD, SIG_IGN) ignorar estes 2 sinais
 *
 * select() -> bloqueia na chamada enquanto espera por msgs udp/tcp
 * setsockopt() -> coloca um temporizador na socket (util para UDP)
 *
 * while (n < MAX_RESEND) {
 * 	int ret = recvfrom(...)
 * 	if (ret < 0)
 * 		if (errno == EWOULDBLOCK || errno == EAGAIN)
 * 			timeout => Resend message
 * }
 */

int main() {
	int fd, errcode;
	ssize_t n;
	struct addrinfo hints, *res;
	struct sockaddr_in addr;
	socklen_t addrlen;
	char buffer[128];

	/*if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		return 1;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	if ((errcode = getaddrinfo(NULL, PORT, &hints, &res)) != 0)
		return 1;
	if ((n = bind(fd, res->ai_addr, res->ai_addrlen)) == -1)
		return 1;

	while (true) {
		addrlen = sizeof(addr);
		if ((n = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr*) &addr, &addrlen)) == -1)
			return 1;

		write(1, "received: ", 10);
		write(1, buffer, n);

		if ((n = sendto(fd, buffer, n, 0, (struct sockaddr*) &addr, addrlen)) == -1)
			return 1;
	}*/

 fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    errcode = getaddrinfo(NULL, PORT, &hints, &res);
    if ((errcode) != 0) {
        exit(1);
    }

    n = bind(fd, res->ai_addr, res->ai_addrlen);
    if (n == -1) {
        exit(1);
    }

    /* Prepara para receber até 5 conexões na socket fd.
    Recusa outras conexões enquanto estiverem 5 conexões pendentes. */
    if (listen(fd, 5) == -1) {
        exit(1);
    }

    /* Loop para processar uma socket de cada vez */
    while (1) {
		int newfd = -1;
        addrlen = sizeof(addr);
        /* Aceita uma nova conexão e cria uma nova socket para a mesma.
        Quando a conexão é aceite, é automaticamente criada uma nova socket
        para ela, guardada no `newfd`.
        Do lado do cliente, esta conexão é feita através da função `connect()`. */
        if ((newfd = accept(fd, (struct sockaddr *)&addr, &addrlen)) == -1) {
            exit(1);
        }

		net::stream<net::tcp_source> s{newfd};
		while (true) {
			auto [res, str] = s.read(1, SIZE_MAX);
			if (res != net::action_status::OK) {
				if (s.found_eom())
					s = net::stream<net::tcp_source>{newfd};
				std::cout << net::status_to_message(res) << "\n";
				continue;
			}
			std::cout << "\"" << str << "\"\n";
		}

        close(newfd);
    }

	freeaddrinfo(res);
	close(fd);
	return 0;
}
