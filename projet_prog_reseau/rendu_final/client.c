#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>

#include "common.h"
#include "include/client.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Missing arguments: server name and port\n");
        return EXIT_FAILURE;
    }

    int sockfd = handle_connect(argv[1], argv[2]);
    // laisse les envois signaler une erreur si la connexion est fermée
    signal(SIGPIPE, SIG_IGN);
    run_poll_loop(sockfd);
    close(sockfd);
    return EXIT_SUCCESS;
}

