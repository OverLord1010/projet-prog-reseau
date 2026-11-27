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
#include "include/client.h"

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

