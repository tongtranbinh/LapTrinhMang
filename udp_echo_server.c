#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <PORT>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    int server = socket(AF_INET, SOCK_DGRAM, 0);
    if (server < 0) {
        perror("socket failed");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(server);
        return 1;
    }

    printf("UDP echo server listening on port %d\n", port);

    while (1) {
        char buffer[1024];
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        ssize_t n = recvfrom(server, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr *)&client_addr, &client_len);
        if (n < 0) {
            perror("recvfrom failed");
            continue;
        }

        buffer[n] = '\0';
        printf("From %s:%d -> %s", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), buffer);

        if (sendto(server, buffer, (size_t)n, 0,
                   (struct sockaddr *)&client_addr, client_len) < 0) {
            perror("sendto failed");
        }
    }

    close(server);
    return 0;
}
