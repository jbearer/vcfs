#include <netinet/ip.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

typedef struct client_connection
{
    int                         fd;
    struct client_connection   *next;
} client_connection;

client_connection *clients = NULL;

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <hook_port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int hookport = atoi(argv[2]);

    int hookfd = socket(AF_INET, SOCK_STREAM, 0);
    if (hookfd < 0) {
        perror("client socket");
        return 1;
    }

    struct sockaddr_in hookaddr = {0};
    hookaddr.sin_family = AF_INET;
    hookaddr.sin_port = htons(hookport);
    hookaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(hookfd, (struct sockaddr *) &hookaddr, sizeof(hookaddr)) < 0) {
        perror("hook bind");
        return 1;
    }

    if (listen(hookfd, 16) < 0) {
        perror("hook listen");
        return 1;
    }

    int serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(serverfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(serverfd, 16) < 0) {
        perror("listen");
        return 1;
    }

    while (true) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(serverfd, &fds);
        FD_SET(hookfd, &fds);
        if (select((serverfd > hookfd ? serverfd : hookfd) + 1, &fds
                    , NULL, NULL, NULL) < 0) {
            perror("select");
            return 1;
        }

        if (FD_ISSET(hookfd, &fds)) {
            int hook_client = accept(hookfd, NULL, NULL);
            if (hook_client < 0) {
                perror("hook accept");
                return 1;
            }

            int size;
            if (read(hook_client, &size, sizeof(size)) != sizeof(size)) {
                perror("read");
                return 1;
            }
            int bufsize = sizeof(size) + size;
            char * buf = (char *) malloc(bufsize);
            if (buf == NULL) {
                perror("malloc");
                return 1;
            }
            *((int *)buf) = size;
            if (read(hook_client, buf+sizeof(size), size) != size) {
                perror("read");
                return 1;
            }
            close(hook_client);

            client_connection * c = clients;
            client_connection * prev = c;
            while (c) {
                if (write(c->fd, buf, bufsize) == -1) {
                    if (errno == EPIPE) {
                        // lost connection to client so free from linked list
                        if (c == clients) {
                            prev = clients->next;
                            close(c->fd);
                            free(c);
                            c = prev;
                            clients = c;
                        } else {
                            prev->next = c->next;
                            close(c->fd);
                            free(c);
                            c = prev->next;
                        }
                        continue;
                    } else {
                        perror("write error not EPIPE");
                    }
                }
                prev = c;
                c = c->next;
            }
        }

        if (FD_ISSET(serverfd, &fds)) {
            int clientfd = accept(serverfd, NULL, NULL);
            if (clientfd < 0) {
                perror("clientfd");
                continue;
            }

            client_connection *conn = (client_connection *)malloc(sizeof(client_connection));
            if (conn == NULL) {
                perror("malloc");
                continue;
            }

            conn->fd = clientfd;
            conn->next = clients;
            clients = conn;
        }
    }
}
