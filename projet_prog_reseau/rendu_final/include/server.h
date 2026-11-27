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
    // Traite une seule trame par appel pour revenir ensuite à la boucle poll.
    struct message msg = {0};
    char payload[MSG_LEN + 1];

    if (recv_frame(sockfd, &msg, payload, sizeof payload - 1) < 0) {
        return -1;
    }

    // Le serveur transmet la réponse à la personne qui a proposé le fichier.
    if (msg.type == FILE_REJECT || msg.type == FILE_ACCEPT) {
        struct connected_client *courant = clients;

        printf("%s %s %s's file.\n", client->nick,
               msg.type == FILE_ACCEPT ? "accepted" : "rejected", msg.infos);

        while(courant != NULL) {
            if (strcmp(msg.infos, courant->nick) == 0) {
                break;
            }
            courant = courant->next;
        }

        if (courant != NULL) {
            snprintf(msg.nick_sender, sizeof msg.nick_sender, "%s", client->nick);
            send_frame(courant->fd, &msg, payload);
        }
        else {
            char* buff = "This user is no longer connected.\n";
            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)strlen(buff);
            send_frame(sockfd, &reply, buff);
        }

        return 0;
    }

    int bytes_read = msg.pld_len;

    // Terminer le texte pour pouvoir utiliser %s et strcmp().
    if (bytes_read > 0 && payload[bytes_read - 1] == '\n') {
        bytes_read--;
    }
    payload[bytes_read] = '\0';

    if (strcmp(payload, "/quit") == 0) {
        printf("%s asked to quit.\n", client->nick);
        return -1;
    }

    else if (msg.type == NICKNAME_NEW) {
        // Vérifie que le nouveau pseudo est libre avant de l'enregistrer.
        struct connected_client *courant = clients;

        while (courant != NULL) {
            if (strcmp(courant->nick, msg.infos) == 0) {
                char* buff = "Nickname already taken.\n";
                

                struct message reply = {0};
                reply.type = ECHO_SEND;
                reply.pld_len = (int)strlen(buff);
                send_frame(sockfd, &reply, buff);

                return 0;
            }
            courant = courant->next;
        }
        printf("%s updated their nickname : %s\n", client->nick, msg.infos);
        // Copie le pseudo dans le champ de taille fixe du client.
        snprintf(client->nick, sizeof client->nick, "%s", msg.infos);

        char buff[MSG_LEN];
        snprintf(buff, sizeof buff, "Your new nickname is %s.\n", client->nick);

        struct message reply = {0};
        reply.type = ECHO_SEND;
        reply.pld_len = (int)strlen(buff);
        send_frame(sockfd, &reply, buff);

        printf("Message sent to %s\n\n", client->nick);

        return 0;
    }

    else if (msg.type == NICKNAME_LIST)
    {
        printf("%s a demandé à voir tous les utilisateurs.\n", client->nick);

        if (clients->next == NULL) {
            char buff[MSG_LEN];
            snprintf(buff, sizeof buff, "You are alone, %s.", client->nick);

            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)strlen(buff);
            send_frame(sockfd, &reply, buff);

            printf("Message sent to %s\n\n", client->nick);

            return 0;
        }
        else {
            char buff[MSG_LEN] = "";
            size_t longueur = 0;
            struct connected_client *courant = clients;

            // Ajoute un pseudo par ligne dans la réponse à /who.
            while (courant != NULL) {
                longueur += snprintf(buff + longueur, sizeof buff - longueur,
                                    "%s\n", courant->nick);
                courant = courant->next;
            }

            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)longueur;

            send_frame(sockfd, &reply, buff);

            return 0;
        }
    }

    else if (msg.type == NICKNAME_INFOS) {
        printf("%s asked to see %s's infos.\n", client->nick, msg.infos);

        struct connected_client *courant = clients;

        while(courant != NULL) {
            if (strcmp(msg.infos, courant->nick) == 0) {
                break;
            }
            courant = courant->next;
        }

        if (courant != NULL) {
            char buff[MSG_LEN];

            char date[32] = "unknown";
            struct tm *heure = localtime(&courant->connection);
            if (heure != NULL) {
                strftime(date, sizeof date, "%Y/%m/%d@%H:%M", heure);
            }

            char ip[NI_MAXHOST];
            char port[NI_MAXSERV];

            // Récupère l'adresse et le port sous forme numérique pour /whois.
            getnameinfo((struct sockaddr *)&courant->addr, courant->addr_len, ip, sizeof ip, port, sizeof port, NI_NUMERICHOST | NI_NUMERICSERV);
            // Une IPv4 peut être représentée en IPv6 avec le préfixe ::ffff:.
            const char *ip_affichee = strncmp(ip, "::ffff:", 7) == 0 ? ip + 7 : ip;
            snprintf(buff, sizeof buff, "%s connected since %s with IP address %s and port number %s.\n", courant->nick, date, ip_affichee, port);

            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)strlen(buff);
            send_frame(sockfd, &reply, buff);

        }
        else {
            char* buff = "This user is not connected.\n";
            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)strlen(buff);
            send_frame(sockfd, &reply, buff);
        }
        return 0;
        
    }
    // Pour /msg, le pseudo du destinataire est dans infos et le texte dans payload.
    else if (msg.type == UNICAST_SEND) {
        struct connected_client *courant = clients;

        printf("%s is trying to send a message to %s.\n", client->nick, msg.infos);

        while(courant != NULL) {
            if (strcmp(msg.infos, courant->nick) == 0) {
                break;
            }
            courant = courant->next;
        }
        
        if (courant != NULL) {
            char buff[MSG_LEN];
            snprintf(buff, sizeof buff, "[%s]: %s", client->nick, payload);
            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)strlen(buff);
            send_frame(courant->fd, &reply, buff);
        }
        else {
            char* buff = "This user is not connected.\n";
            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)strlen(buff);
            send_frame(sockfd, &reply, buff);
        }

        return 0;
    }

    else if (msg.type == BROADCAST_SEND) {
        struct connected_client *courant = clients;

        char buff[MSG_LEN];
        snprintf(buff, sizeof buff, "[%s]: %s", client->nick, payload);
        struct message reply = {0};
        reply.type = ECHO_SEND;
        reply.pld_len = (int)strlen(buff);

        printf("%s is trying to broadcast a message.\n", client->nick);

        if (courant->next == NULL) {
            char* buff = "You are alone, thus you cannot broadcast a message.\n";
            struct message reply = {0};
            reply.type = ECHO_SEND;
            reply.pld_len = (int)strlen(buff);
            send_frame(sockfd, &reply, buff);

            return 0;
        }

        // Diffuse le message à tous les autres clients.
        while (courant != NULL) {
            if (courant->fd != sockfd) {
                send_frame(courant->fd, &reply, buff);
            }
            courant = courant->next;
        }

        return 0;
    }

    // Transmet le nom du fichier au destinataire pour lui demander son accord.
    else if (msg.type == FILE_REQUEST) {
        struct connected_client *courant = clients;

        printf("%s is trying to send a file to %s.\n", client->nick, msg.infos);

        while(courant != NULL) {
            if (strcmp(msg.infos, courant->nick) == 0) {
                break;
            }
            courant = courant->next;
        }

        if (courant != NULL) {
            struct message reply = {0};
            reply.type = FILE_REQUEST;
            snprintf(reply.nick_sender, sizeof reply.nick_sender, "%s", client->nick);
            reply.pld_len = bytes_read;
            send_frame(courant->fd, &reply, payload);
        }
        else {
            char* buff = "This user is not connected.\n";
            struct message reply = {0};
            reply.type = FILE_REJECT;
            snprintf(reply.infos, sizeof reply.infos, "%s", msg.infos);
            reply.pld_len = (int)strlen(buff);
            send_frame(sockfd, &reply, buff);
        }

        return 0;
    }

    // Une ligne de texte ordinaire reçoit une réponse en écho.
    printf("Message from %d: %s\n", sockfd, payload);

    char buff[MSG_LEN];
    int max_text = (int)(sizeof buff - sizeof "We received '' from you.");
    snprintf(buff, sizeof buff, "We received '%.*s' from you.", max_text, payload);

    struct message reply = {0};
    reply.type = ECHO_SEND;
    reply.pld_len = (int)strlen(buff);
    send_frame(sockfd, &reply, buff);

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
	// Avec AI_PASSIVE, getaddrinfo prépare les adresses locales pour l'écoute.
	if (getaddrinfo(NULL, serv_port, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	// Essaie les adresses proposées jusqu'à ce que bind réussisse.
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

    struct pollfd fds[MAX_CONN] = {0};

    // poll ignore les cases dont le descripteur vaut -1
    for(int i = 0; i < MAX_CONN; i++) {
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }

    // La case 0 surveille les nouvelles connexions, les autres suivent les clients.
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
                    // Insère le nouveau client en tête de la liste.
                    client->next = clients;
                    clients = client;

                    fds[j] = (struct pollfd){ .fd = new_fd, .events = POLLIN };
                }
            } else if (fds[i].fd >= 0 && fds[i].fd != listen_fd) {
                if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
                    continue;  // aucun événement à traiter pour ce client
                }

                int fd = fds[i].fd;
                // Garde l’adresse du pointeur à modifier si ce client quitte la liste.
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

                // Rend la case disponible pour une prochaine connexion.
                close(fd);
                fds[i] = (struct pollfd){ .fd = -1 };
            }
        }
    }
}
