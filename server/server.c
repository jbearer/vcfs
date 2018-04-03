#include <netinet/ip.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

typedef struct client_connection
{
    int                         fd;
    struct client_connection   *next;
} client_connection;

client_connection *clients = NULL;

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(sockfd, 16) < 0) {
        perror("listen");
        return 1;
    }

    while (true) {
        int clientfd = accept(sockfd, NULL, NULL);
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
