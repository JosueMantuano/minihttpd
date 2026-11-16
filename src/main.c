#include "server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int server_fd;

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return EXIT_FAILURE;
    }

    server_fd = server_create_listener(argv[1]);
    if (server_fd < 0)
        return EXIT_FAILURE;

    printf("Servidor escuchando en el puerto %s\n", argv[1]);

    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;

            perror("accept");
            close(server_fd);
            return EXIT_FAILURE;
        }

        puts("Cliente conectado");
        close(client_fd);
    }
}