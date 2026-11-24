#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include "common.h"

int echo_server(int sockfd) {
    int to_read = rcv_next_message_size(sockfd);

    if (to_read <= 0) {
        return -1;  // déconnexion ou erreur de lecture
    }

    printf("Received %d bytes from %d\n", to_read, sockfd);

    char* msg = malloc(sizeof(char)*to_read);
    int bytes_read = rcv_message(sockfd, msg, to_read);
    if(bytes_read == -1) {
        free(msg);
        return 1;
    }
    // Terminer le texte pour pouvoir utiliser %s et strcmp().

    if (bytes_read > 0 && msg[bytes_read - 1] == '\n') {
        bytes_read--;
        }
    msg[bytes_read] = '\0';

	if (strcmp(msg, "/quit") == 0)
	{
		printf("Le client %d a demandé à quitter.\n", sockfd);
		free(msg);
		close(sockfd);
		return 0;
	}

	printf("Message from %d: %s\n", sockfd, msg);

    char buff[MSG_LEN] = {0};

    snprintf(buff, sizeof(buff), "We received '%s' from you.", msg);

    // Sending message
    int to_send = strlen(buff) + 1;
    send_next_message_size(sockfd, to_send);
    send_message(sockfd, buff, to_send);

    printf("Message sent to %d\n\n", sockfd);

    return 0;
}

int handle_bind(const char* serv_port) {
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if (getaddrinfo(NULL, serv_port, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,
		rp->ai_protocol);
		if (sfd == -1) {
			continue;
		}
		if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {
			break;
		}
		close(sfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "Could not bind\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(result);
	return sfd;
}

int main(int argc, char** argv) {

    if (argc < 2) {
        printf("Missing argument: server port\n");
        return EXIT_FAILURE;
    }

    const char *serv_port = argv[1];

	struct sockaddr cli;
	int listen_fd;//, connfd;
	socklen_t len;
    len = sizeof(cli);
	listen_fd = handle_bind(serv_port);
	if ((listen(listen_fd, SOMAXCONN)) != 0) {
		perror("listen()\n");
		exit(EXIT_FAILURE);
	}
    printf("listening on port %s\n", serv_port);

    // Gestion de plusieurs client en simultané
    struct pollfd fds[MAX_CONN] = {0};

    // poll ignore les cases dont le descripteur vaut -1
    for(int i = 0; i < MAX_CONN; i++) {
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }

    // La case 0 accueille la socket d’écoute, les autres pour les clients
    fds[0].fd = listen_fd;
    fds[0].events = POLLHUP | POLLIN;
    fds[0].revents = 0;

    while(1) {
        int nfds = poll(fds, MAX_CONN, -1);
        die(nfds, "On polling");

        for(int i = 0; i < MAX_CONN; i++) {
            // sur la socket d’écoute POLLIN annonce une connexion à accepter
            if(fds[i].fd == listen_fd && (fds[i].revents & POLLIN)) {
                // accept() crée une socket dédiée au client et listen_fd reste en écoute
                int new_fd = accept(listen_fd, (struct sockaddr*)&cli, &len);
                die(new_fd, "On accepting");

                // chercher une case libre, la case 0 est réservée à la socket d'écoute
                int j;
                for (j = 1; j < MAX_CONN; j++) {
                    if (fds[j].fd == -1) // -1 indique une case inutilisée
                        break;
                }

                if (j == MAX_CONN) {
                    // aucune place disponible
                    close(new_fd);
                } else {
                    fds[j].fd = new_fd;    // ajouter le client dans la case libre
                    fds[j].events = POLLIN; // surveiller les données à lire sur ce client
                    fds[j].revents = 0;    // effacer les événements de cette case
                }
            } else if (fds[i].fd >= 0 && fds[i].fd != listen_fd) {
                if (fds[i].revents & POLLIN) {
                    if (echo_server(fds[i].fd) == 0) {
                        continue;  // passer à la case suivante du tableau
                    }
                } else if (!(fds[i].revents & (POLLHUP | POLLERR))) {
                    continue;  // aucun événement à traiter pour ce client
                }

                close(fds[i].fd);
                fds[i].fd = -1;
                fds[i].events = 0;
                fds[i].revents = 0;
            }
        }
    }
	
	close(listen_fd);
	return EXIT_SUCCESS;
}

