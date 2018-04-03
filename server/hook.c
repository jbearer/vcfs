#include <netinet/ip.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in hookaddr = {0};
    hookaddr.sin_family = AF_INET;
    hookaddr.sin_port = htons(port);
    hookaddr.sin_addr.s_addr = htonl(2887533628);

    if (connect(sockfd, (struct sockaddr *) &hookaddr, sizeof(hookaddr)) < 0) {
        perror("hook connection");
        return 1;
    }

    char buf[26 + sizeof(int)];
    *(int *)buf = 26;
    for (int i = 0; i < 26; ++i) {
        buf[sizeof(int) + i] = 'a' + i;
    }

    if (write(sockfd, buf, sizeof(int) + 26) != sizeof(int) + 26) {
        perror("write");
        return 1;
    }
}