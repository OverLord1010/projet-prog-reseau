#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"

void echo_client(int sockfd) {
	char buff[MSG_LEN];
	int n;
	while (1) {
		// Cleaning memory
		memset(buff, 0, MSG_LEN);
		
		// Getting message from client
		n = 0;
		while ((buff[n++] = getchar()) != '\n') {} // trailing '\n' will be sent

		// Sending message
		send_next_message_size(sockfd, n);
		send_message(sockfd, buff, n);

		printf("Message sent!\n");
		// Cleaning memory
		memset(buff, 0, MSG_LEN);
		// Receiving message
		int to_read = rcv_next_message_size(sockfd);
		if (to_read <= 0 || to_read > MSG_LEN) {
			break;
		}

		if (rcv_message(sockfd, buff, to_read) < 0) {
			break;
		}
		printf("Received: %s\n", buff);
	}
}

int handle_connect(const char *serv_addr, const char *serv_port) {
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(serv_addr, serv_port, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,rp->ai_protocol);
		if (sfd == -1) {
			continue;
		}
		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
			break;
		}
		close(sfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(result);
	return sfd;
}

int main(int argc, char** argv) {

	if (argc < 3) {
		printf("Missing arguments: server name and port\n");
		return EXIT_FAILURE;
	}

    const char *serv_addr = argv[1];
    const char *serv_port = argv[2];

	int sfd;
	sfd = handle_connect(serv_addr, serv_port);
	echo_client(sfd);
	close(sfd);
	return EXIT_SUCCESS;
}

