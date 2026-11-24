#define MSG_LEN 1024
#define SERV_PORT "8080"
#define SERV_ADDR "127.0.0.1"
#define MAX_CONN 20
#define NICK_LEN 128
#define INFOS_LEN 128

struct info{
    short s;
    long l;
};

enum msg_type {
    NICKNAME_NEW,
    NICKNAME_LIST,
    NICKNAME_INFOS,
    ECHO_SEND,
    UNICAST_SEND,
    BROADCAST_SEND,
    MULTICAST_CREATE,
    MULTICAST_LIST,
    MULTICAST_JOIN,
    MULTICAST_SEND,
    MULTICAST_QUIT,
    FILE_REQUEST,
    FILE_ACCEPT,
    FILE_REJECT,
    FILE_SEND,
    FILE_ACK
};

struct message {
    int pld_len;
    char nick_sender[NICK_LEN];
    enum msg_type type;
    char infos[INFOS_LEN];
};

static char *msg_type_str[] = {
    "NICKNAME_NEW", "NICKNAME_LIST", "NICKNAME_INFOS", "ECHO_SEND",
    "UNICAST_SEND", "BROADCAST_SEND", "MULTICAST_CREATE", "MULTICAST_LIST",
    "MULTICAST_JOIN", "MULTICAST_SEND", "MULTICAST_QUIT", "FILE_REQUEST",
    "FILE_ACCEPT", "FILE_REJECT", "FILE_SEND", "FILE_ACK"
};

// Arrêter le programme si un appel système renvoie une erreur (valeur négative).
int die(int ret_value, char* msg) {
    if(ret_value < 0) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
    return 0;
}

// Lire l’entier qui annonce le nombre d’octets du prochain message.
int rcv_next_message_size(int sockfd) {
    int bytes_read = 0;
    int to_read = 0;
    int ret = 0;

    // TCP peut livrer l’entier en plusieurs lectures : compléter les octets manquants.
    while(bytes_read != sizeof(int)) {
        ret = read(sockfd, (char*)&to_read+bytes_read, sizeof(int)-bytes_read);
        die(ret, "On reading msg size");
        // read() == 0 : le client a fermé la connexion ; éviter une boucle infinie.
        if(ret == 0) {
            return -1;
        }
        bytes_read += ret;
    }

    return to_read;
}

// Lire exactement to_read octets, même si plusieurs read() sont nécessaires.
int rcv_message(int sockfd, void* buf, int to_read) {
    int bytes_read = 0;
    int ret = 0;

    while(bytes_read != to_read) {
        ret = read(sockfd, (char*)buf+bytes_read, to_read-bytes_read);
        die(ret, "On reading");
        // read() == 0 : le client a fermé la connexion ; éviter une boucle infinie.
        if(ret == 0) {
            return -1;
        }
        bytes_read += ret;
    }

    return bytes_read;
}

// Envoyer d’abord la taille du message : TCP ne sépare pas les messages.
int send_next_message_size(int sockfd, int size) {
    int bytes_sent = 0;
    int ret = 0;
    
    // write() peut envoyer moins d’octets que demandé : envoyer le reste en boucle.
    while(bytes_sent != sizeof(int)) {
        ret = write(sockfd, (char*)&size+bytes_sent, sizeof(int)-bytes_sent);
        die(ret, "On sending msg size");
        bytes_sent += ret;
    }

    return 0;
}

// Envoyer le contenu en reprenant après les octets déjà transmis.
int send_message(int sockfd, void* buf, int size) {
    int bytes_sent = 0;
    int ret = 0;

    while(bytes_sent != size) {
        ret = write(sockfd, (char*)buf+bytes_sent, size-bytes_sent);
        die(ret, "On writing");
        bytes_sent += ret;
    }

    return 0;
}