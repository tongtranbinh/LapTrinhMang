#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <IP> <PORT>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int client = socket(AF_INET, SOCK_DGRAM, 0);
    if (client < 0) {
        perror("socket failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    printf("UDP echo client to %s:%d\n", ip, port);

    char buffer[1024];
    while (1) {
        printf("Enter message (exit to quit): ");
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }

        size_t len = strlen(buffer);
        if (len == 0) {
            continue;
        }

        if (sendto(client, buffer, len, 0,
                   (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("sendto failed");
            break;
        }

        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t n = recvfrom(client, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr *)&from_addr, &from_len);
        if (n < 0) {
            perror("recvfrom failed");
            break;
        }

        buffer[n] = '\0';
        printf("Echo: %s", buffer);

        if (strncmp(buffer, "exit", 4) == 0) {
            break;
        }
    }

    close(client);
    return 0;
}
