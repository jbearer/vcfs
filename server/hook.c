#include <netinet/ip.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
        return 1;
    }

    const char *ip_str = argv[1];
    int port = atoi(argv[2]);

    int ip_bytes[4];
    if (sscanf(ip_str, "%d.%d.%d.%d", ip_bytes, ip_bytes + 1, ip_bytes + 2, ip_bytes + 3) != 4) {
        fprintf(stderr, "Invalid ip address %s\n", ip_str);
        return 1;
    }
    unsigned long ip =
        (unsigned long)ip_bytes[3] + ip_bytes[2]*256 + ip_bytes[1]*256*256 + ip_bytes[0]*256*256*256;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in hookaddr = {0};
    hookaddr.sin_family = AF_INET;
    hookaddr.sin_port = htons(port);
    hookaddr.sin_addr.s_addr = htonl(ip);

    if (connect(sockfd, (struct sockaddr *) &hookaddr, sizeof(hookaddr)) < 0) {
        perror("hook connection");
        return 1;
    }

    char buf[26 + sizeof(int)];
    *(uint32_t *)buf = htonl(26);
    for (int i = 0; i < 26; ++i) {
        buf[sizeof(int) + i] = 'a' + i;
    }

    if (write(sockfd, buf, sizeof(int) + 26) != sizeof(int) + 26) {
        perror("write");
        return 1;
    }
}