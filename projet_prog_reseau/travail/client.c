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

//#define isauth "False"

const char *fonction(const char *commande, char arg[NICK_LEN]) {
    char cmd[32];

    // who

    int n = sscanf(commande, "%31s %31[^\n]", cmd, arg);

    if (n >= 1 && strcmp(cmd, "/nick") == 0)
    {
        if (n == 2){
            printf("Nouveau pseudo : %s\n", arg);
            return arg;
        }
        else
            printf("Erreur dans le pseudo\n");
    }

    else if (n >= 1 && strcmp(cmd, "/whois") == 0)
    {
        if (n == 2) {
            printf("Qui est : %s ?\n", arg);
            return arg;
        }
        else printf("Erreur dans le whois\n");
    }

    else if (n >= 1 && strcmp(cmd, "/msgall") == 0)
    {
        if (n == 2)
        {
            printf("Message à tous : %s\n", arg);
            return arg;
        }
        else
            printf("Erreur dans le msgall\n");
    }
        return "";
}

static void run_poll_loop(int sockfd) {
    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = sockfd,      .events = POLLIN }
    };
    char payload[MSG_LEN];
    char * isauth = "False";

        while (1)
    {
        // attendre une saisie ou des données du serveur
        if (poll(fds, 2, -1) < 0) {
            perror("poll");
            break;
        }

        // socket pret, recevoir et afficher le message
        if (fds[1].revents & POLLIN) {
            struct message msg = {0};
            msg.type = ECHO_SEND;
            if (rcv_message(sockfd, &msg, sizeof msg) < 0) {
                break;
            }

            if (rcv_message(sockfd, payload, msg.pld_len) < 0) {
                break;
            }

            if (strcmp(payload, "Nickname already taken.\n") != 0)
                isauth = "True";

            printf("Received: %.*s\n", msg.pld_len, payload);
        }

        if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }

        // clavier pret, lire et envoyer la ligne
        if (fds[0].revents & POLLIN) {
            if (fgets(payload, sizeof payload, stdin) == NULL) {
                break;
            }

            char cmd[32];
            char arg[NICK_LEN];
            int n = sscanf(payload, "%31s %31[^\n]", cmd, arg);
            if (n < 1) {
                continue;
            }

            if (strcmp(isauth, "False") == 0) {
                if (strcmp(cmd, "/quit") == 0)
                {
                    struct message msg = {0};
                    msg.type = ECHO_SEND;
                    msg.pld_len = (int)strlen(payload);

                    send_message(sockfd, &msg, sizeof msg);
                    send_message(sockfd, payload, msg.pld_len);
                }


                if (n == 2 && strcmp(cmd, "/nick") == 0)
                {
                    struct message msg = {0};
                    msg.type = NICKNAME_NEW;
                    msg.pld_len = (int)strlen(payload);
                    snprintf(msg.infos, sizeof msg.infos, "%s", arg);

                    send_message(sockfd, &msg, sizeof msg);
                    send_message(sockfd, payload, msg.pld_len);
                }
                
                else
                {
                    printf("Veuillez d'abord vous authentifier avec la commande /nick <pseudo>\n");
                }
            } else {
                struct message msg = {0};
                msg.pld_len = (int)strlen(payload);

                if (strcmp(payload, "/who\n") == 0)
                {
                    msg.type = NICKNAME_LIST;
                    msg.pld_len = (int)strlen(payload);

                    send_message(sockfd, &msg, sizeof msg);
                    send_message(sockfd, payload, msg.pld_len);

                    continue;
                }


                msg.type = ECHO_SEND;
                send_message(sockfd, &msg, sizeof msg);
                send_message(sockfd, payload, msg.pld_len);

                if (strcmp(payload, "/quit\n") != 0)
                {
                    char arg[NICK_LEN];
                    const char *test = fonction(payload, arg);
                    printf("Message sent!%s\n", test);
                }
                
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

    int sockfd = handle_connect(argv[1], argv[2]);
    run_poll_loop(sockfd);
    close(sockfd);
    return EXIT_SUCCESS;
}

