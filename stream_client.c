#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4) {
        printf("Usage: %s <IP> <PORT> [ROUNDS]\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int rounds = (argc == 4) ? atoi(argv[3]) : 20;
    if (rounds <= 0) {
        rounds = 20;
    }

    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        perror("socket failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(client, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        close(client);
        return 1;
    }

    printf("Connected to %s:%d\n", ip, port);

    const char *payload =
        "SOICTSOICT012345678901234567890123456789012345"
        "6789SOICTSOICTSOICT012345678901234567890123456"
        "7890123456789012345678901234567890123456789SOI"
        "CTSOICT01234567890123456789012345678";

    size_t payload_len = strlen(payload);
    int chunk_pattern[] = {7, 3, 11, 5, 13, 2, 17, 4};
    int chunk_count = (int)(sizeof(chunk_pattern) / sizeof(chunk_pattern[0]));

    for (int r = 0; r < rounds; r++) {
        size_t offset = 0;
        int idx = 0;

        while (offset < payload_len) {
            int chunk = chunk_pattern[idx % chunk_count];
            idx++;

            if (offset + (size_t)chunk > payload_len) {
                chunk = (int)(payload_len - offset);
            }

            ssize_t sent = send(client, payload + offset, (size_t)chunk, 0);
            if (sent <= 0) {
                perror("send failed");
                close(client);
                return 1;
            }

            offset += (size_t)sent;
            usleep(20000);
        }

        usleep(50000);
    }

    printf("Sent %d rounds of streaming text.\n", rounds);
    close(client);
    return 0;
}
