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
#include "include/server.h"

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

