#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>

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

int main() { // NOT FUNCTIONAL
	struct sockaddr_in addr;
	socklen_t addrlen;
	char buffer[128];

	struct sigaction act;

	memset(&act, 0, size_of act);
	act.sa_handler=SIG_IGN;

	if(sigaction(SIGPIPE, &act, NULL) == -1) // sigpipe
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

static void startUDPServer() {
	int fd_UDP, errcode;
	ssize_t n_UDP;
	struct addrinfo hints_UDP, *res_UDP;
	struct sockaddr_in addr;
	socklen_t addrlen;

	if ((fd_UDP = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		return 1;
	memset(&hints_UDP, 0, sizeof(hints_UDP));
	hints_UDP.ai_family = AF_INET;
	hints_UDP.ai_socktype = SOCK_DGRAM;
	hints_UDP.ai_flags = AI_PASSIVE;

	if ((errcode = getaddrinfo(NULL, PORT, &hints_UDP, &res_UDP)) != 0)
		return 1;

	if ((n_UDP = bind(fd_UDP, res_UDP->ai_addr, res_UDP->ai_addrlen)) == -1)
		return 1;

	while(true) {
		nread = recvfrom(fd_UDP, buffer, size_of(buffer), 0, (struct sockaddr)* &addr, &addrlen);
		if(nread == -1)
			return 1;
		//process command(...)
		// ans = ...

		n=sendto(fd_UDP, buffer, size_of(ans), 0, (struct sockaddr)&addr, &addrlen):
		if(n==-1)
			return 1;
	}
}

static void startTCPServer() {
	int fd_TCP, errcode;
	ssize_t n_TCP;
	struct addrinfo hints_TCP, *res_TCP;
	struct sockaddr_in addr;
	socklen_t addrlen;


	if ((fd_TCP = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		return 1;
	memset(&hints_TCP, 0, sizeof(hints_TCP));
	hints_TCP.ai_family = AF_INET;
	hints_TCP.ai_socktype = SOCK_STREAM;
	hints_TCP.ai_flags = AI_PASSIVE;

	if ((errcode = getaddrinfo(NULL, PORT, &hints_TCP, &res_TCP)) != 0)
		return 1;
	
	if ((n_TCP = bind(fd_TCP, res_TCP->ai_addr, res_TCP->ai_addrlen)) == -1)
		return 1;
	if(listen(fd_TCP, 5) == -1)
		return 1;

	while(true) {
		addrlen = size_of(addr);
		if((newfd=accept(fd_TCP, (struct sockaddr*) &addr, &addrlen)) == -1)
			return 1; 
		while((n=read(newfd, buffer, size_of(buffer)) != 0)) {
			if (n==-1)
				return 1;
			// process command (...)
			// ans = ...
			nwritten = 0
			while (nwritten != sizeof(ans)) {
				if((nw=write(newfd, ptr, n)) <= 0)
					return 1;
				nwritten += nw;
			}
			close(newfd);
		}
	}
}