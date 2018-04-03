#include <netinet/ip.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ip> <port> <branch>\n", argv[0]);
        return 1;
    }

    const char *ip_str = argv[1];
    int port = atoi(argv[2]);
    const char *branch_name = argv[3];
    uint32_t branch_name_len = strlen(branch_name);

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

    ssize_t bufsize = sizeof(uint32_t) + branch_name_len;
    char * buf = malloc(bufsize);
    *(uint32_t *)buf = htonl(branch_name_len);
    strncpy(buf + sizeof(uint32_t), branch_name, branch_name_len);

    if (write(sockfd, buf, bufsize) != bufsize) {
        perror("write");
        return 1;
    }

    free(buf);
}