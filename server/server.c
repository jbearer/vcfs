#include <netinet/ip.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

typedef struct client_connection
{
    int                         fd;
    struct client_connection   *prev;
    struct client_connection   *next;
} client_connection;

client_connection *clients = NULL;

/**
 * Initialie a TCP server at the given port and begin listening for connections.
 *
 * Returns a file descriptor for the server.
 */
int init_tcp_server(int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        abort();
    }

    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

    struct sockaddr_in hookaddr = {0};
    hookaddr.sin_family = AF_INET;
    hookaddr.sin_port = htons(port);
    hookaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, (struct sockaddr *) &hookaddr, sizeof(hookaddr)) < 0) {
        perror("bind");
        abort();
    }

    if (listen(sockfd, 16) < 0) {
        perror("listen");
        abort();
    }

    return sockfd;
}

/**
 * Add a file descriptor to the list of active clients.
 */
void add_client(int fd)
{
    client_connection *conn = (client_connection *)malloc(sizeof(client_connection));
    if (conn == NULL) {
        perror("malloc");
        return;
    }

    conn->fd = fd;
    if (clients) {
        clients->prev = conn;
    }
    conn->next = clients;
    conn->prev = NULL;
    clients = conn;
}

/**
 * Remove an active client, freeing its resources and closing the TCP connection.
 */
client_connection * remove_client(client_connection *c)
{
    if (c->prev) {
        c->prev->next = c->next;
    }
    if (c->next) {
        c->next->prev = c->prev;
    }
    if (c == clients) {
        clients = c->next;
    }

    client_connection *next = c->next;

    close(c->fd);
    free(c);

    return next;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <hook_port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int hookport = atoi(argv[2]);

    int hookfd = init_tcp_server(hookport);
    int serverfd = init_tcp_server(port);

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
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

            uint32_t size;
            if (read(hook_client, &size, sizeof(size)) != sizeof(size)) {
                perror("read");
                return 1;
            }
            size = ntohl(size);

            int bufsize = sizeof(size) + size;
            char * buf = (char *) malloc(bufsize);
            if (buf == NULL) {
                perror("malloc");
                return 1;
            }
            *((int *)buf) = htonl(size);
            if (read(hook_client, buf+sizeof(size), size) != size) {
                perror("read");
                free(buf);
                return 1;
            }
            close(hook_client);
            LOG("Recieved message %.*s", bufsize, buf);

            client_connection * c = clients;
            while (c) {
                if (write(c->fd, buf, bufsize) == -1) {
                    if (errno == EPIPE) {
                        LOG("removing client %d", c->fd);
                        c = remove_client(c);
                        continue;
                    } else {
                        perror("write error not EPIPE");
                    }
                }
                LOG("sending message to client %d", c->fd);
                c = c->next;
            }

            free(buf);
        }

        if (FD_ISSET(serverfd, &fds)) {
            int clientfd = accept(serverfd, NULL, NULL);
            if (clientfd < 0) {
                perror("clientfd");
                continue;
            }

            LOG("adding client %d", clientfd);
            add_client(clientfd);
        }
    }
}
