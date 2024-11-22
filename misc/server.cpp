#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

#define PORT "58011"

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

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
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
	}

	freeaddrinfo(res);
	close(fd);
	return 0;
}
