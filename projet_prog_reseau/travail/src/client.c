#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#include "common.h"

void echo_client(int sockfd) {
    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = sockfd,      .events = POLLIN }
    };
    char buff[MSG_LEN];

    while (1) {
        // attendre une saisie ou des données du serveur
        if (poll(fds, 2, -1) < 0) {
            perror("poll");
            break;
        }

        // socket pret, recevoir et afficher le message
        if (fds[1].revents & POLLIN) {
            int size = rcv_next_message_size(sockfd);
            if (size <= 0 || size > MSG_LEN) {
                break;
            }

            if (rcv_message(sockfd, buff, size) < 0) {
                break;
            }

            printf("Received: %.*s\n", size, buff);
        }

        if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }

        // clavier pret, lire et envoyer la ligne
        if (fds[0].revents & POLLIN) {
            if (fgets(buff, sizeof buff, stdin) == NULL) {
                break;
            }

            int size = (int)strlen(buff);
            send_next_message_size(sockfd, size);
            send_message(sockfd, buff, size);

			if (strcmp(buff, "/quit\n") != 0) {
				printf("Message sent!\n");
			}
        }

        if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }
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

