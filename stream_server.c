#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int count_matches(const char *text, size_t text_len, const char *pattern, size_t pattern_len) {
    int count = 0;
    if (text_len < pattern_len) {
        return 0;
    }

    for (size_t i = 0; i + pattern_len <= text_len; i++) {
        if (memcmp(text + i, pattern, pattern_len) == 0) {
            count++;
        }
    }
    return count;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <PORT>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    const char *pattern = "0123456789";
    const size_t pattern_len = strlen(pattern);

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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

    if (listen(server, 1) < 0) {
        perror("listen failed");
        close(server);
        return 1;
    }

    printf("Streaming server listening on port %d\n", port);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client = accept(server, (struct sockaddr *)&client_addr, &client_len);
    if (client < 0) {
        perror("accept failed");
        close(server);
        return 1;
    }

    printf("Client connected from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    char recv_buf[32];
    char tail[16] = {0};
    size_t tail_len = 0;
    long long total_count = 0;

    while (1) {
        ssize_t n = recv(client, recv_buf, sizeof(recv_buf), 0);
        if (n < 0) {
            perror("recv failed");
            break;
        }
        if (n == 0) {
            printf("Client disconnected.\n");
            break;
        }

        char combined[64];
        size_t combined_len = tail_len + (size_t)n;

        memcpy(combined, tail, tail_len);
        memcpy(combined + tail_len, recv_buf, (size_t)n);

        int found = count_matches(combined, combined_len, pattern, pattern_len);
        total_count += found;

        printf("Received %zd bytes, new matches: %d, total: %lld\n", n, found, total_count);

        if (combined_len >= pattern_len - 1) {
            tail_len = pattern_len - 1;
            memcpy(tail, combined + combined_len - tail_len, tail_len);
        } else {
            tail_len = combined_len;
            memcpy(tail, combined, tail_len);
        }
    }

    close(client);
    close(server);
    return 0;
}
