#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/param.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {

    int listen_port;
    char *target_ip;
    int target_port;
    int listen_fd;
    int modbus_fd;
    int client_fd;
    int opt = 1;
    struct sockaddr_in addr;
    fd_set saved_fds;
    fd_set working_fds;
    int max_fd;
    char buffer[BUFFER_SIZE];

    if (argc < 4) {
        fprintf(stderr, "usage: %s <listen_port> <target_ip> <target_port>\n", argv[0]);
        return 1;
    }

    listen_port = atoi(argv[1]);
    target_ip = argv[2];
    target_port = atoi(argv[3]);

    /* listen socket */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        fprintf(stderr, "listen socket creation failed");
        return 1;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 10);

    /* modbus server socket */
    modbus_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (modbus_fd == -1) {
        fprintf(stderr, "modbus socket creation failed");
        return 1;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(502);
    if (inet_pton(AF_INET, target_ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "invalid address %s", target_ip);
        return 1;
    }
    if (setsockopt(modbus_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
        fprintf(stderr, "setsockopt TCP_NODELAY failed");
        return 1;
    }
    if (setsockopt(modbus_fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) == -1) {
        fprintf(stderr, "setsockopt SO_KEEPALIVE failed");
        return 1;
    }
    if (connect(modbus_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "connection to %s:%d failed", target_ip, target_port);
        return 1;
    }

    FD_ZERO(&saved_fds);
    FD_SET(listen_fd, &saved_fds);
    max_fd = listen_fd;
    printf("modbus proxy active: local:%d -> remote:%s:%d\n", listen_port, target_ip, target_port);

    while (1) {
        working_fds = saved_fds;
        if (select(max_fd + 1, &working_fds, NULL, NULL, NULL) < 0) {
            break;
        }

        for (int i = 0; i <= max_fd; i++) {
            if (!FD_ISSET(i, &working_fds)) {
                continue;
            }
            if (i == listen_fd) {
                /* new client connection */
                client_fd = accept(listen_fd, NULL, NULL);
                setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
                setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
                FD_SET(client_fd, &saved_fds);
                max_fd = MAX(client_fd, max_fd);
            } else {
                /* request from a client */
                ssize_t bytes = recv(i, buffer, sizeof(buffer), 0);

                if (bytes <= 0) {
                    /* Connection closed or error */
                    close(i);
                    FD_CLR(i, &saved_fds);
                    if (i == max_fd) {
                        do {
                            i--;
                        } while ((i >= 0) && !FD_ISSET(i, &saved_fds));
                        max_fd = i;
                    }
                } else {
                    /* Forward the Modbus */
                    send(modbus_fd, buffer, bytes, 0);
                    /* Wait for Modbus response */
                    bytes = recv(modbus_fd, buffer, sizeof (buffer), 0);
                    /* Forward response to requesting client */
                    send(i, buffer, bytes, 0);
                }
            }
        }
    }
    return 0;
}
