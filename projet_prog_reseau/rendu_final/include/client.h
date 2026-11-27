#include <sys/stat.h>
#include <sys/time.h>

// ouvre une socket pour recevoir la connexion directe
static int open_listen_socket(int sockfd, char *adresse, size_t taille)
{
    // Reprend l’adresse locale utilisée pour communiquer avec le serveur.
    struct sockaddr_in chat;
    socklen_t len = sizeof(chat);
    if (getsockname(sockfd, (struct sockaddr *)&chat, &len) == -1)
    {
        perror("getsockname()");
        return -1;
    }

    if (chat.sin_family != AF_INET) {
        fprintf(stderr, "La connexion de chat doit utiliser IPv4 pour le transfert.\n");
        return -1;
    }

    char ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &chat.sin_addr, ip, sizeof ip) == NULL) {
        perror("inet_ntop");
        return -1;
    }

    struct sockaddr_in ecoute = chat;
    // laisse le système choisir un port libre sur la même adresse locale
    ecoute.sin_port = 0;
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1)
    {
        perror("socket()");
        return -1;
    }

    if (bind(sfd, (struct sockaddr *)&ecoute, sizeof ecoute) == -1)
    {
        perror("bind()");
        close(sfd);
        return -1;
    }
    if (listen(sfd, 1) == -1)
    {
        perror("listen()");
        close(sfd);
        return -1;
    }

    len = sizeof(ecoute);
    if (getsockname(sfd, (struct sockaddr *)&ecoute, &len) == -1)
    {
        perror("getsockname()");
        close(sfd);
        return -1;
    }
    // prépare l’adresse à communiquer à l’expéditeur
    snprintf(adresse, taille, "%s:%u", ip, (unsigned)ntohs(ecoute.sin_port));
    return sfd;
}

// mesure le temps écoulé sans dépendre de l’heure système
static int64_t maintenant_ms(void)
{
    struct timespec instant;

    clock_gettime(CLOCK_MONOTONIC, &instant);
    return (int64_t)instant.tv_sec * 1000 + instant.tv_nsec / 1000000;
}

// se connecte au destinataire avec l’adresse reçue via le serveur
static int connect_direct(const char *adresse)
{
    char ip[INET_ADDRSTRLEN];
    // sépare l’adresse ipv4 du port
    const char *separateur = strchr(adresse, ':');

    if (separateur == NULL || separateur == adresse ||
        (size_t)(separateur - adresse) >= sizeof ip) {
        fprintf(stderr, "Adresse directe invalide.\n");
        return -1;
    }

    size_t longueur_ip = (size_t)(separateur - adresse);
    memcpy(ip, adresse, longueur_ip);
    ip[longueur_ip] = '\0';

    const char *texte_port = separateur + 1;
    if (*texte_port == '\0' ||
        texte_port[strspn(texte_port, "0123456789")] != '\0') {
        fprintf(stderr, "Port direct invalide.\n");
        return -1;
    }

    long port = strtol(texte_port, NULL, 10);
    if (port < 1 || port > 65535) {
        fprintf(stderr, "Port direct hors limites.\n");
        return -1;
    }

    struct sockaddr_in bob = {0};
    bob.sin_family = AF_INET;
    bob.sin_port = htons((unsigned short)port);

    if (inet_pton(AF_INET, ip, &bob.sin_addr) != 1) {
        fprintf(stderr, "IPv4 directe invalide.\n");
        return -1;
    }

    int direct_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (direct_fd == -1) {
        perror("socket directe");
        return -1;
    }

    // passe en mode non bloquant pour limiter l’attente à cinq secondes
    int flags = fcntl(direct_fd, F_GETFL);
    if (flags == -1 || fcntl(direct_fd, F_SETFL, flags | O_NONBLOCK) == -1)
        goto echec;

    if (connect(direct_fd, (struct sockaddr *)&bob, sizeof bob) == -1) {
        if (errno != EINPROGRESS)
            goto echec;

        struct pollfd attente = { .fd = direct_fd, .events = POLLOUT };
        int64_t expiration_ms = maintenant_ms() + 5000;
        for (;;) {
            int64_t restant = expiration_ms - maintenant_ms();
            if (restant <= 0) {
                errno = ETIMEDOUT;
                goto echec;
            }

            int resultat = poll(&attente, 1, (int)restant);
            if (resultat > 0)
                break;
            if (resultat < 0 && errno != EINTR)
                goto echec;
        }

        // vérifie si la connexion a réussi après le réveil de poll
        int erreur = 0;
        socklen_t len = sizeof erreur;
        if (getsockopt(direct_fd, SOL_SOCKET, SO_ERROR, &erreur, &len) == -1)
            goto echec;
        if (erreur != 0) {
            errno = erreur;
            goto echec;
        }
    }

    // rétablit le mode initial de la socket
    if (fcntl(direct_fd, F_SETFL, flags) == -1)
        goto echec;

    return direct_fd;

echec:
    perror("connect direct");
    close(direct_fd);
    return -1;
}

// ferme la socket d’écoute et efface la demande reçue
static void annuler_demande(struct pollfd *ecoute,
                            char *demande_de, char *nom_recu)
{
    if (ecoute->fd >= 0)
        close(ecoute->fd);

    ecoute->fd = -1;
    demande_de[0] = '\0';
    nom_recu[0] = '\0';
}


static void envoyer_fichier(int fd, const char *chemin)
{
    FILE *fichier = fopen(chemin, "rb");
    if (fichier == NULL) {
        perror(chemin);
        goto nettoyage;
    }

    const char *slash = strrchr(chemin, '/');
    const char *nom = slash ? slash + 1 : chemin;

    unsigned char bloc[4096];
    struct message msg = {0};
    msg.type = FILE_SEND;
    snprintf(msg.infos, sizeof msg.infos, "%s", nom);

    // Lit le fichier par blocs pour éviter de le charger entièrement en mémoire.
    size_t lus;
    while ((lus = fread(bloc, 1, sizeof bloc, fichier)) > 0) {
        msg.pld_len = (int)lus;
        if (send_frame(fd, &msg, bloc) < 0) {
            fprintf(stderr, "Impossible d'envoyer un bloc.\n");
            goto nettoyage;
        }
    }

    if (ferror(fichier)) {
        perror("fread");
        goto nettoyage;
    }

    // Une trame FILE_SEND vide marque la fin du fichier.
    msg.pld_len = 0;
    if (send_frame(fd, &msg, NULL) < 0) {
        fprintf(stderr, "Impossible d'envoyer le marqueur de fin.\n");
        goto nettoyage;
    }

    // Limite chaque lecture de la confirmation à dix secondes.
    struct timeval delai = { .tv_sec = 10, .tv_usec = 0 };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &delai, sizeof delai) < 0) {
        perror("setsockopt");
        fprintf(stderr, "Transfert non confirmé.\n");
        goto nettoyage;
    }

    struct message ack;
    if (recv_frame(fd, &ack, NULL, 0) < 0 ||
        ack.type != FILE_ACK ||
        ack.pld_len != 0 ||
        strcmp(ack.infos, msg.infos) != 0) {
        fprintf(stderr, "Transfert non confirmé : ACK absent ou invalide.\n");
        goto nettoyage;
    }

    puts("Bob a confirmé la sauvegarde du fichier.");

nettoyage:
    if (fichier != NULL)
        fclose(fichier);
    close(fd);
}

static void recevoir_fichier(int fd, const char *nom_accepte)
{
    FILE *destination = NULL;
    int temp_fd = -1;
    int temporaire_cree = 0;
    char temporaire[] = ".resa1/inbox/.reception-XXXXXX";
    char chemin_final[sizeof ".resa1/inbox/" + INFOS_LEN];

    if (nom_accepte[0] == '\0' || strcmp(nom_accepte, ".") == 0 ||
        strcmp(nom_accepte, "..") == 0 || strchr(nom_accepte, '/') != NULL ||
        strlen(nom_accepte) >= INFOS_LEN) {
        fprintf(stderr, "Nom de fichier invalide.\n");
        goto nettoyage;
    }

    if (mkdir(".resa1", 0700) == -1 && errno != EEXIST) {
        perror(".resa1");
        goto nettoyage;
    }
    if (mkdir(".resa1/inbox", 0700) == -1 && errno != EEXIST) {
        perror(".resa1/inbox");
        goto nettoyage;
    }

    snprintf(chemin_final, sizeof chemin_final,
             ".resa1/inbox/%s", nom_accepte);

    // Écrit dans un fichier temporaire tant que la réception est en cours.
    temp_fd = mkstemp(temporaire);
    if (temp_fd == -1) {
        perror("mkstemp");
        goto nettoyage;
    }
    temporaire_cree = 1;

    destination = fdopen(temp_fd, "wb");
    if (destination == NULL) {
        perror("fdopen");
        goto nettoyage;
    }
    temp_fd = -1; // fclose ferme désormais ce descripteur

    unsigned char bloc[4096];
    struct message msg;

    for (;;) {
        if (recv_frame(fd, &msg, bloc, sizeof bloc) < 0) {
            fprintf(stderr, "Réception interrompue ou trame invalide.\n");
            goto nettoyage;
        }

        // Chaque trame doit correspondre au fichier accepté.
        if (msg.type != FILE_SEND || strcmp(msg.infos, nom_accepte) != 0) {
            fprintf(stderr, "Trame inattendue pendant le transfert.\n");
            goto nettoyage;
        }

        if (msg.pld_len == 0)
            break;

        size_t taille = (size_t)msg.pld_len;
        if (fwrite(bloc, 1, taille, destination) != taille) {
            perror("fwrite");
            goto nettoyage;
        }
    }

    int resultat = fclose(destination);
    destination = NULL; // le flux est fermé même si fclose signale une erreur
    if (resultat == EOF) {
        perror("fclose");
        goto nettoyage;
    }

    // Donne son nom définitif au fichier sans écraser un fichier existant.
    if (link(temporaire, chemin_final) == -1) {
        if (errno == EEXIST)
            fprintf(stderr, "Sauvegarde refusée : %s existe déjà.\n",
                    chemin_final);
        else
            perror("link");
        goto nettoyage;
    }

    printf("Fichier sauvegardé : %s\n", chemin_final);

    // Confirme la sauvegarde à l’expéditeur sur la connexion directe.
    struct message ack = {0};
    ack.type = FILE_ACK;
    snprintf(ack.infos, sizeof ack.infos, "%s", nom_accepte);
    if (send_frame(fd, &ack, NULL) < 0)
        fprintf(stderr, "Impossible d'envoyer l'ACK.\n");

nettoyage:
    if (destination != NULL)
        fclose(destination);
    if (temp_fd >= 0)
        close(temp_fd);
    if (temporaire_cree && unlink(temporaire) == -1)
        perror("unlink du fichier temporaire");
    close(fd);
}

static void run_poll_loop(int sockfd) {
    // surveille le clavier, le serveur et la socket d’écoute si elle est ouverte
    struct pollfd fds[3] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = sockfd,      .events = POLLIN },
        { .fd = -1 }
    };

    char payload[MSG_LEN + 1];
    char * isauth = "False";

    // garde le fichier et le destinataire en attendant sa réponse
    struct {
        char chemin[MSG_LEN + 1];
        char pseudo[NICK_LEN];
        int en_attente;
    } transfert = {0};
    // Mémorise l’expéditeur et le nom du fichier proposé.
    char demande_de[NICK_LEN] = "";
    char nom_recu[MSG_LEN + 1] = "";

    int64_t expiration_ms = 0;

    while (1)
    {
        // attend sans limite sauf si une connexion directe est attendue
        int timeout = -1;

        if (fds[2].fd >= 0) {
            int64_t restant = expiration_ms - maintenant_ms();

            if (restant <= 0) {
                puts("Délai de connexion directe expiré.");
                annuler_demande(&fds[2], demande_de, nom_recu);
            } else {
                timeout = (int)restant;
            }
        }

        int resultat = poll(fds, 3, timeout);
        if (resultat < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }
        if (resultat == 0)
            continue;

        if (fds[2].fd >= 0 && maintenant_ms() >= expiration_ms) {
            puts("Délai de connexion directe expiré.");
            annuler_demande(&fds[2], demande_de, nom_recu);
        }

        if (fds[2].fd >= 0 && (fds[2].revents & POLLIN)) {
            // accepte la connexion puis libère la socket d’écoute
            int direct_fd = accept(fds[2].fd, NULL, NULL);

            if (direct_fd < 0) {
                perror("accept");
            } else {
                puts("Connexion directe reçue");
                recevoir_fichier(direct_fd, nom_recu);
            }

            annuler_demande(&fds[2], demande_de, nom_recu);
        }

        // lit une trame envoyée par le serveur
        if (fds[1].revents & POLLIN) {
            struct message msg = {0};
            if (recv_frame(sockfd, &msg, payload, sizeof payload - 1) < 0)
                break;

            payload[msg.pld_len] = '\0';


            // refuse une nouvelle demande si une autre est déjà en cours
            if (msg.type == FILE_REQUEST) {
                if (demande_de[0] != '\0' || fds[2].fd >= 0) {
                    struct message refus = {0};
                    refus.type = FILE_REJECT;
                    snprintf(refus.infos, sizeof refus.infos, "%s", msg.nick_sender);

                    if (send_frame(sockfd, &refus, NULL) < 0) {
                        fprintf(stderr, "Impossible d'envoyer le refus.\n");
                        break;
                    }

                    continue;
                }

                snprintf(demande_de, sizeof demande_de, "%s", msg.nick_sender);
                snprintf(nom_recu, sizeof nom_recu, "%s", payload);
                printf("%s is trying to send you this file : %s.\nAccept ? [Y/N]\n",
                    demande_de, nom_recu);
                continue;
            }

            // associe la réponse à la demande envoyée
            if (msg.type == FILE_ACCEPT || msg.type == FILE_REJECT) {
                if (transfert.en_attente &&
                    (strcmp(msg.nick_sender, transfert.pseudo) == 0 ||
                     (msg.nick_sender[0] == '\0' &&
                      strcmp(msg.infos, transfert.pseudo) == 0))) {
                    if (msg.nick_sender[0] != '\0') {
                        printf("%s %s your file.\n", msg.nick_sender,
                               msg.type == FILE_ACCEPT ? "accepted" : "rejected");
                        if (msg.type == FILE_ACCEPT)
                            printf("Adresse directe : %s\n", payload);
                    } else {
                        printf("%s", payload);
                    }

                    if (msg.type == FILE_REJECT)
                        memset(&transfert, 0, sizeof transfert);

                    if (msg.type == FILE_ACCEPT) {
                        // L’accord contient l’adresse où joindre le destinataire.
                        int direct_fd = connect_direct(payload);
                        if (direct_fd >= 0) {
                            puts("Connexion directe établie");
                            envoyer_fichier(direct_fd, transfert.chemin);
                        }

                        memset(&transfert, 0, sizeof transfert);
                        continue;
                    }
            
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

        // lit une ligne saisie au clavier
        if (fds[0].revents & POLLIN) {
            if (fgets(payload, sizeof payload, stdin) == NULL) {
                break;
            }

            // traite y ou n comme une réponse à la demande reçue
            if (demande_de[0] != '\0' &&
                (strcmp(payload, "Y\n") == 0 || strcmp(payload, "N\n") == 0)) {
                if (fds[2].fd >= 0)
                    continue;

                struct message reponse = {0};
                char adresse[NI_MAXHOST + 8];
                const char *contenu = NULL;

                reponse.type = FILE_REJECT;
                snprintf(reponse.infos, sizeof reponse.infos, "%s", demande_de);

                if (strcmp(payload, "Y\n") == 0) {
                    // Ouvre l’écoute avant de communiquer l’adresse à l’expéditeur.
                    int fd = open_listen_socket(sockfd, adresse, sizeof(adresse));

                    if (fd >= 0) {
                        fds[2].fd = fd;
                        fds[2].events = POLLIN;

                        reponse.type = FILE_ACCEPT;
                        reponse.pld_len = (int)strlen(adresse);
                        contenu = adresse;
                    }
                }

                if (send_frame(sockfd, &reponse, contenu) < 0) {
                    fprintf(stderr, "Impossible d'envoyer la réponse.\n");
                    break;
                }

                if (reponse.type == FILE_ACCEPT) {
                    // donne dix secondes à l’expéditeur pour se connecter
                    expiration_ms = maintenant_ms() + 10000;
                }
                else{
                    annuler_demande(&fds[2], demande_de, nom_recu);
                }
                continue;
            }

            // récupère le premier mot et le début de l’argument
            char cmd[32];
            char arg[NICK_LEN];
            int n = sscanf(payload, "%31s %31[^\n]", cmd, arg);
            if (n < 1) {
                continue;
            }

            // gère le choix du pseudo avant les commandes de chat
            if (strcmp(isauth, "False") == 0) {
                if (strcmp(cmd, "/quit") == 0)
                {
                    struct message msg = {0};
                    msg.type = ECHO_SEND;
                    msg.pld_len = (int)strlen(payload);

                    send_frame(sockfd, &msg, payload);
                    continue;
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
                    // repère le début du texte à diffuser
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

                    // reprend le message complet après le pseudo
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

                    // retire les guillemets autour du chemin s’il y en a
                    size_t longueur = strlen(chemin);
                    if (longueur >= 2 && chemin[0] == '"' && chemin[longueur - 1] == '"') {
                        chemin[longueur - 1] = '\0';
                        chemin++;
                    }

                    // extrait le nom du fichier à envoyer au destinataire
                    const char *dernier_slash = strrchr(chemin, '/');
                    const char *nom = dernier_slash ? dernier_slash + 1 : chemin;
                    size_t longueur_nom = strlen(nom);

                    if (longueur_nom == 0 || longueur_nom >= sizeof msg.infos) {
                        printf("Nom de fichier vide ou trop long.\n");
                        continue;
                    }

                    // vérifie que le fichier peut être ouvert en lecture
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

                    // Garde le chemin local pour ouvrir le fichier après acceptation.
                    strcpy(transfert.chemin, chemin);
                    strcpy(transfert.pseudo, pseudo);
                    transfert.en_attente = 1;
                    continue;
                }

                // envoie les autres lignes au serveur en mode écho
                msg.type = ECHO_SEND;
                send_frame(sockfd, &msg, payload);

            }                    
        }

        if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }
    }

    annuler_demande(&fds[2], demande_de, nom_recu);

}

int handle_connect(const char *serv_addr, const char *serv_port) {
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	// Résout le nom ou l’adresse du serveur pour ouvrir la connexion de chat.
	if (getaddrinfo(serv_addr, serv_port, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	// essaie les adresses du serveur jusqu’à la première connexion réussie
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

