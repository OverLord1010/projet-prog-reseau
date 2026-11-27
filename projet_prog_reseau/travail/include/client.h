static void run_poll_loop(int sockfd) {
    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = sockfd,      .events = POLLIN }
    };
    char payload[MSG_LEN + 1];
    char * isauth = "False";

    struct {
        char chemin[MSG_LEN + 1];
        char pseudo[NICK_LEN];
        int en_attente;
    } transfert = {0};
    char demande_de[NICK_LEN] = "";

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
            if (recv_frame(sockfd, &msg, payload, sizeof payload - 1) < 0)
                break;

            payload[msg.pld_len] = '\0';

            if (msg.type == FILE_REQUEST) {
                snprintf(demande_de, sizeof demande_de, "%s", msg.nick_sender);
                printf("%s", payload);
                continue;
            }
            if (msg.type == FILE_REJECT) {
                if (transfert.en_attente &&
                    (strcmp(msg.nick_sender, transfert.pseudo) == 0 ||
                     (msg.nick_sender[0] == '\0' &&
                      strcmp(msg.infos, transfert.pseudo) == 0))) {
                    printf("%s", payload);
                    memset(&transfert, 0, sizeof transfert);
                }
                continue;
            }
            if (strcmp(payload, "Nickname already taken.\n") != 0)
                isauth = "True";

            printf("%.*s\n", msg.pld_len, payload);
        }

        if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }

        // clavier pret, lire et envoyer la ligne
        if (fds[0].revents & POLLIN) {
            if (fgets(payload, sizeof payload, stdin) == NULL) {
                break;
            }

            if (demande_de[0] != '\0' && strcmp(payload, "N\n") == 0) {
                struct message refus = {0};
                refus.type = FILE_REJECT;
                refus.pld_len = 0;
                snprintf(refus.infos, sizeof refus.infos, "%s", demande_de);

                if (send_frame(sockfd, &refus, NULL) < 0) {
                    fprintf(stderr, "Impossible d'envoyer le refus.\n");
                    break;
                }

                demande_de[0] = '\0';
                continue;
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

                    send_frame(sockfd, &msg, payload);
                }


                if (n == 2 && strcmp(cmd, "/nick") == 0)
                {
                    struct message msg = {0};
                    msg.type = NICKNAME_NEW;
                    msg.pld_len = (int)strlen(payload);
                    snprintf(msg.infos, sizeof msg.infos, "%s", arg);

                    send_frame(sockfd, &msg, payload);
                }
                
                else
                {
                    printf("Veuillez d'abord vous authentifier avec la commande /nick <pseudo>\n");
                }
            } else {
                char cmd[32];
                char arg[NICK_LEN];
                int n = sscanf(payload, "%31s %31[^\n]", cmd, arg);
                if (n < 1)
                {
                    continue;
                }

                struct message msg = {0};
                msg.pld_len = (int)strlen(payload);

                if (strcmp(cmd, "/nick") == 0)
                {
                    if (n != 2) {
                        printf("Utilisation : /nick <pseudo>\n");
                        continue;
                    }
                    msg.type = NICKNAME_NEW;
                    msg.pld_len = (int)strlen(payload);
                    snprintf(msg.infos, sizeof msg.infos, "%s", arg);

                    send_frame(sockfd, &msg, payload);

                    continue;
                }

                if (strcmp(cmd, "/who") == 0)
                {
                    if (n != 1){
                        printf("Erreur dans la commande.\nVeuillez utiliser la commande par /who\n\n");
                        continue;
                    }
                    msg.type = NICKNAME_LIST;
                    msg.pld_len = (int)strlen(payload);

                    send_frame(sockfd, &msg, payload);

                    continue;
                }

                if (strcmp(cmd, "/whois") == 0)
                {
                    if (n != 2)
                    {
                        printf("Erreur dans la commande.\nVeuillez utiliser la commande par /whois <pseudo>\n\n");
                        continue;
                    }
                    msg.type = NICKNAME_INFOS;
                    msg.pld_len = (int)strlen(payload);
                    snprintf(msg.infos, sizeof msg.infos, "%s", arg);

                    send_frame(sockfd, &msg, payload);

                    continue;
                }

                if (strcmp(cmd, "/msgall") == 0)
                {
                    int debut_texte = 0;
                    if (n != 2)
                    {
                        printf("Erreur dans la commande.\nVeuillez utiliser la commande par /msgall <message>\n\n");
                        continue;
                    }
                    sscanf(payload, "%*s %n", &debut_texte);
                    char *texte = payload + debut_texte;

                    msg.type = BROADCAST_SEND;
                    msg.pld_len = (int)strlen(texte);

                    send_frame(sockfd, &msg, texte);

                    continue;
                }

                if (strcmp(cmd, "/msg") == 0) {
                    char pseudo[NICK_LEN];
                    int debut_texte = 0;

                    if (sscanf(payload, "%*s %127s %n", pseudo, &debut_texte) != 1 ||
                        payload[debut_texte] == '\0') {
                        printf("Utilisation : /msg <pseudo> <message>\n");
                        continue;
                    }

                    char *texte = payload + debut_texte;

                    msg.type = UNICAST_SEND;
                    snprintf(msg.infos, sizeof msg.infos, "%s", pseudo);
                    msg.pld_len = (int)strlen(texte);

                    send_frame(sockfd, &msg, texte);
                    continue;
                }

                if (strcmp(cmd, "/send") == 0)
                {
                    char pseudo[NICK_LEN];
                    int debut_fichier = 0;

                    if (sscanf(payload, "%*s %127s %n", pseudo, &debut_fichier) != 1 ||
                        payload[debut_fichier] == '\0')
                    {
                        printf("Utilisation : /send <pseudo> <fichier>\n");
                        continue;
                    }

                    if (transfert.en_attente) {
                        printf("Une demande de transfert est déjà en attente.\n");
                        continue;
                    }

                    char *chemin = payload + debut_fichier;
                    chemin[strcspn(chemin, "\r\n")] = '\0';

                    // Retirer les guillemets autour du chemin, s'il y en a.
                    size_t longueur = strlen(chemin);
                    if (longueur >= 2 && chemin[0] == '"' && chemin[longueur - 1] == '"') {
                        chemin[longueur - 1] = '\0';
                        chemin++;
                    }

                    // Prendre ce qui suit le dernier '/'. Un chemin relatif peut ne pas en avoir.
                    const char *dernier_slash = strrchr(chemin, '/');
                    const char *nom = dernier_slash ? dernier_slash + 1 : chemin;
                    size_t longueur_nom = strlen(nom);

                    if (longueur_nom == 0 || longueur_nom >= sizeof msg.infos) {
                        printf("Nom de fichier vide ou trop long.\n");
                        continue;
                    }

                    FILE *fichier = fopen(chemin, "rb");
                    if (fichier == NULL) {
                        perror(chemin);
                        continue;
                    }
                    fclose(fichier);

                    msg.type = FILE_REQUEST;
                    snprintf(msg.infos, sizeof msg.infos, "%s", pseudo);
                    msg.pld_len = (int)longueur_nom;

                    if (send_frame(sockfd, &msg, nom) < 0) {
                        fprintf(stderr, "Impossible d'envoyer la demande de transfert.\n");
                        break;
                    }

                    strcpy(transfert.chemin, chemin);
                    strcpy(transfert.pseudo, pseudo);
                    transfert.en_attente = 1;
                    continue;
                }

                msg.type = ECHO_SEND;
                send_frame(sockfd, &msg, payload);

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
