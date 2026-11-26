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
#include <time.h> 

#include "common.h"

struct connected_client {
    int fd;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    char nick[16];
    time_t connection;
    struct connected_client *next;
};

int echo_server(int sockfd, struct connected_client *client,
                struct connected_client *clients) {
    struct message msg;
    if (rcv_message(sockfd, &msg, sizeof msg) < 0) {
        return -1;
    }

    char payload[MSG_LEN + 1];
    if (msg.pld_len < 0 || msg.pld_len > MSG_LEN) {
        return -1;
    }
    int bytes_read = rcv_message(sockfd, payload, msg.pld_len);
    if (bytes_read < 0) {
        return -1;
    }

    // Terminer le texte pour pouvoir utiliser %s et strcmp().
    if (bytes_read > 0 && payload[bytes_read - 1] == '\n') {
        bytes_read--;
    }
    payload[bytes_read] = '\0';

    if (strcmp(payload, "/quit") == 0) {
        printf("%s a demandé à quitter.\n", client->nick);
        return -1;
    }

    else if (msg.type == NICKNAME_NEW) {
        struct connected_client *courant = clients;

        while (courant != NULL) {
            if (strcmp(courant->nick, msg.infos) == 0) {
                char* buff = "Nickname already taken.\n";
                

                struct message reply = {0};
                reply.type = ECHO_SEND;
                reply.pld_len = (int)strlen(buff);
                send_message(sockfd, &reply, sizeof reply);
                send_message(sockfd, buff, reply.pld_len);

                return 0;
            }
            courant = courant->next;
        }
        printf("%s a changé de pseudo : %s\n", client->nick, msg.infos);
        // permet de tronquer le pseudo s'il est trop long
        snprintf(client->nick, sizeof client->nick, "%s", msg.infos);

        char buff[MSG_LEN];
        snprintf(buff, sizeof buff, "Your new nickname is %s.\n", client->nick);

        struct message reply = {0};
        reply.type = ECHO_SEND;
        reply.pld_len = (int)strlen(buff);
        send_message(sockfd, &reply, sizeof reply);
        send_message(sockfd, buff, reply.pld_len);

        printf("Message sent to %s\n\n", client->nick);

        return 0;
    }

    else if (msg.type == NICKNAME_LIST)
    {
        printf("%s a demandé à voir tous les utilisateurs.\n", client->nick);

        if (clients->next == NULL) {
            printf("Le client est seul sur le serveur.\n");
            char buff[MSG_LEN];
            snprintf(buff, sizeof buff, "You are alone, %s.", client->nick);

            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)strlen(buff);
            send_message(sockfd, &reply, sizeof reply);
            send_message(sockfd, buff, reply.pld_len);

            printf("Message sent to %s\n\n", client->nick);

            return 0;
        }
        else {
            char buff[MSG_LEN] = "";
            size_t longueur = 0;
            struct connected_client *courant = clients;

            while (courant != NULL) {
                longueur += snprintf(buff + longueur, sizeof buff - longueur,
                                    "%s\n", courant->nick);
                courant = courant->next;
            }

            struct message reply = {0};
            reply.type = ECHO_SEND;  // Même type que ta réponse « You are alone »
            reply.pld_len = (int)longueur;

            send_message(sockfd, &reply, sizeof reply);
            send_message(sockfd, buff, reply.pld_len);

            return 0;
        }
    }

    else if (msg.type == NICKNAME_INFOS) {
        printf("%s a demandé à voir les infos de %s.\n", client->nick, msg.infos);

        struct connected_client *courant = clients;

        while(strcmp(courant->nick, msg.infos) != 0) {
            courant = courant->next;
        }

        if (courant != NULL) {
            char buff[MSG_LEN];

            char date[32] = "inconnue";
            struct tm *heure = localtime(&courant->connection);
            if (heure != NULL) {
                strftime(date, sizeof date, "%Y/%m/%d@%H:%M", heure);
            }

            char ip[NI_MAXHOST];
            char port[NI_MAXSERV];

            getnameinfo((struct sockaddr *)&courant->addr, courant->addr_len, ip, sizeof ip, port, sizeof port, NI_NUMERICHOST | NI_NUMERICSERV);
            snprintf(buff, sizeof buff, "%s connected since %s with IP address %s and port number %s.\n", courant->nick, date, ip, port);

            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)strlen(buff);
            send_message(sockfd, &reply, sizeof reply);
            send_message(sockfd, buff, reply.pld_len);

        }
        else {
            char* buff = "This user is not connected.\n";
            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)strlen(buff);
            send_message(sockfd, &reply, sizeof reply);
            send_message(sockfd, buff, reply.pld_len);
        }
        return 0;
        
    }

    printf("Message from %d: %s\n", sockfd, payload);

    char buff[MSG_LEN];
    int max_text = (int)(sizeof buff - sizeof "We received '' from you.");
    snprintf(buff, sizeof buff, "We received '%.*s' from you.", max_text, payload);

    struct message reply = {0};
    reply.type = ECHO_SEND;
    reply.pld_len = (int)strlen(buff);
    send_message(sockfd, &reply, sizeof reply);
    send_message(sockfd, buff, reply.pld_len);

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

static void run_poll_loop(int listen_fd) {
    struct connected_client *clients = NULL;

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
                struct sockaddr_storage cli = {0};
                socklen_t len = sizeof cli;

                int new_fd = accept(listen_fd, (struct sockaddr *)&cli, &len);
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
                    struct connected_client *client = malloc(sizeof *client);
                    if (client == NULL) {
                        close(new_fd);
                        continue;
                    }

                    client->fd = new_fd;
                    client->addr = cli;
                    client->addr_len = len;
                    strcpy(client->nick, "new_user");
                    client->connection = time(NULL);
                    client->next = clients;
                    clients = client;

                    fds[j] = (struct pollfd){ .fd = new_fd, .events = POLLIN };
                }
            } else if (fds[i].fd >= 0 && fds[i].fd != listen_fd) {
                if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
                    continue;  // aucun événement à traiter pour ce client
                }

                int fd = fds[i].fd;
                struct connected_client **link = &clients;

                while (*link != NULL && (*link)->fd != fd) {
                    link = &(*link)->next;
                }

                if (*link != NULL) {
                    struct connected_client *client = *link;
                    if ((fds[i].revents & POLLIN) &&
                        echo_server(fd, client, clients) == 0) {
                        continue;
                    }
                    *link = client->next;
                    free(client);
                }

                close(fd);
                fds[i] = (struct pollfd){ .fd = -1 };
            }
        }
    }
}

int main(int argc, char** argv) {

    if (argc < 2) {
        printf("Missing argument: server port\n");
        return EXIT_FAILURE;
    }

    const char *serv_port = argv[1];

	int listen_fd;
	listen_fd = handle_bind(serv_port);
	if ((listen(listen_fd, SOMAXCONN)) != 0) {
		perror("listen()\n");
		exit(EXIT_FAILURE);
	}
    printf("listening on port %s\n", serv_port);

    run_poll_loop(listen_fd);


	close(listen_fd);
	return EXIT_SUCCESS;
}

