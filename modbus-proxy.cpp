#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <errno.h>

#define BUFFER_SIZE 1024

static int open_server_fd(const char *server_ip, int server_port, struct sockaddr_in *server_addr) {

    int fd;
    int opt = 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "modbus socket creation failed");
        return 1;
    }
    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr->sin_addr) <= 0) {
        fprintf(stderr, "invalid address %s", server_ip);
        return -1;
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
        fprintf(stderr, "setsockopt TCP_NODELAY failed");
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) == -1) {
        fprintf(stderr, "setsockopt SO_KEEPALIVE failed");
        return -1;
    }
    
    int idle = 5; 
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));

    int interval = 2; 
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));

    int max_pings = 3; 
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &max_pings, sizeof(max_pings));

    return fd;

}

struct command {
    uint16_t transaction_id;
    uint16_t protocol_id;
    uint16_t length;
    uint8_t unit_id;
    uint8_t function_code;
    uint8_t data[32];
};

#define MAX_CLIENTS 10
struct client {
    int fd;
    uint16_t transaction_id[0x10000];
    bool used;
} clients[MAX_CLIENTS];

struct client *server_transaction_id_to_client[0x10000];

#if 0
int get_command(uint8_t *buffer, ssize_t size, struct command *cmd) {

    cmd->transaction_id = (buffer[0] << 8) | buffer[1];
    cmd->protocol_id = (buffer[2] << 8) | buffer[3];
    cmd->length = (buffer[4] << 8) | buffer[5];
    cmd->unit_id = buffer[6];
    cmd->function_code = buffer[7];
    memcpy(cmd->data, &buffer[8], cmd->length - 2);

    return cmd->length + 6;
}
#endif
ssize_t set_transaction_id(uint8_t *buffer, ssize_t size, uint16_t new_transaction_id, uint16_t *old_transaction_id) {

    if (old_transaction_id) {
        *old_transaction_id = (buffer[0] << 8) + buffer[1];
    }
    buffer[0] = new_transaction_id >> 8;
    buffer[1] = new_transaction_id & 0xff;
    
    return (buffer[4] << 8) + buffer[5] + 6;
}

uint16_t get_transaction_id(uint8_t *buffer, ssize_t size) {
   
    return (buffer[0] << 8) + buffer[1];
}

static int get_max_fd(fd_set *fds, int max_fd) {

    int fd = max_fd;
    do {
        fd--;
    } while ((fd >= 0) && !FD_ISSET(fd, fds));

    return fd;
}
#if 0
static void printf_fds(fd_set *fds) {

    for (int i = 0; i < 10; i++) {
       if (FD_ISSET(i, fds)) {
           printf("1");
       } else {
           printf("0");
       }
    }
    printf("\n");
}
#endif
int main(int argc, char *argv[]) {

    int listen_port;
    char *server_ip;
    int server_port;
    int listen_fd;
    int server_fd;
    int client_fd;
    struct sockaddr_in listen_addr;
    struct sockaddr_in server_addr;
    fd_set saved_fds;
    fd_set working_fds;
    int max_fd = 0;
    uint8_t buffer[BUFFER_SIZE];
    bool server_connected = false;
    int ret;
    int opt = 1;
    uint16_t server_transaction_id = 0;
    struct client *c;
    int rfd;
    ssize_t i;
    ssize_t len;
    ssize_t cmd_len;
    ssize_t recv_len;

    if (argc < 4) {
        fprintf(stderr, "usage: %s <listen_port> <server_ip> <server_port>\n", argv[0]);
        return 1;
    }

    listen_port = atoi(argv[1]);
    server_ip = argv[2];
    server_port = atoi(argv[3]);

    FD_ZERO(&saved_fds);

    /* listen socket */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        fprintf(stderr, "listen socket creation failed");
        return 1;
    }
    FD_SET(listen_fd, &saved_fds);
    max_fd = MAX(listen_fd, max_fd);
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(listen_port);
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    bind(listen_fd, (struct sockaddr*)&listen_addr, sizeof(listen_addr));
    listen(listen_fd, 10);

    server_fd = open_server_fd(server_ip, server_port, &server_addr);
    FD_SET(server_fd, &saved_fds);
    max_fd = MAX(server_fd, max_fd);
    printf("modbus proxy active: listen port:%d -> remote:%s:%d\n", listen_port, server_ip, server_port);

    memset(clients, 0, sizeof(clients));

    while (1) {
        working_fds = saved_fds;
        ret = select(max_fd + 1, &working_fds, NULL, NULL, NULL);
        if (ret < 0) {
            fprintf(stderr, "select error");
            break;
        }
        for (rfd = 0; rfd <= max_fd; rfd++) {
            if (!FD_ISSET(rfd, &working_fds)) {
                continue;
            }
            if (rfd == listen_fd) {
                /* new client connection */
                client_fd = accept(listen_fd, NULL, NULL);
                setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
                setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
                FD_SET(client_fd, &saved_fds);
                max_fd = MAX(max_fd, client_fd);
                for (i = 0, c = clients; (i < MAX_CLIENTS) && c->used; i++, c++);
                if (i < MAX_CLIENTS) {
                    c->fd = client_fd;
                    c->used = true;
                    memset(c->transaction_id, 0, sizeof(c->transaction_id));
                }
            } else if (rfd == server_fd) {
                recv_len = recv(server_fd, buffer, sizeof (buffer), 0);
                if (recv_len <= 0) {
                    if (server_connected) {
                        close(server_fd);
                        FD_CLR(server_fd, &saved_fds);
                        max_fd = get_max_fd(&saved_fds, max_fd);
                        server_fd = open_server_fd(server_ip, server_port, &server_addr);
                        memset(server_transaction_id_to_client, 0, sizeof (server_transaction_id_to_client));
                        for (i = 0, c = clients; i < MAX_CLIENTS; i++, c++) {
                            if (c->used) {
                               close(c->fd);
                               c->used = false;
                               memset(c->transaction_id, 0, sizeof(c->transaction_id));
                            }
                        }
                        FD_SET(server_fd, &saved_fds);
                        max_fd = MAX(max_fd, server_fd);
                    }
                    if (connect(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
                        server_connected = true;
                    } else {
                        server_connected = false;
                    }
                } else {
                    len = 0;
                    do {
                        /* response from server */
                        uint16_t transaction_id;
                        transaction_id = get_transaction_id(buffer + len, recv_len - len);
                        c = server_transaction_id_to_client[transaction_id];
                        if (!c) {
                            fprintf(stderr, "transaction ID to client translation error");
                            continue;
                        }
                        cmd_len = set_transaction_id(buffer + len, recv_len - len, c->transaction_id[transaction_id], 0);
                        /* Forward response to requesting client */
                        ret = send(c->fd, buffer + len, cmd_len, 0);
                        len += cmd_len;
                    } while (len < recv_len);
                }
            } else {
                /* request from a client */
                for (i = 0, c = clients; (i < MAX_CLIENTS) && (c->fd != rfd); i++, c++);
                if (i >= MAX_CLIENTS) {
                    fprintf(stderr, "client not found");
                    continue;
                }
                recv_len = recv(rfd, buffer, sizeof(buffer), 0);
                if (recv_len <= 0) {
                    /* Connection closed or error */
                    close(rfd);
                    c->used = false;
                    FD_CLR(rfd, &saved_fds);
                    max_fd = get_max_fd(&saved_fds, max_fd);
                } else if (server_connected) {
                    len = 0;
                    do {
                        cmd_len = set_transaction_id(buffer + len, recv_len - len, server_transaction_id, &c->transaction_id[server_transaction_id]);
                        server_transaction_id_to_client[server_transaction_id] = c;
                        server_transaction_id++;
                        /* forward to server */  
                        ret = send(server_fd, buffer + len, cmd_len, 0);
                        len += cmd_len;
                    } while (len < recv_len);
                }
            }
        }
    }
    return 0;
}
