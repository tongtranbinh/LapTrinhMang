#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <PORT> <log_file>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    char *logfile = argv[2];

    int server = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server, (struct sockaddr*)&addr, sizeof(addr));
    listen(server, 5);

    printf("Server running on port %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);

        int client = accept(server, (struct sockaddr*)&client_addr, &len);

        char buffer[256];
        int n = recv(client, buffer, sizeof(buffer)-1, 0);
        if (n > 0) {
            buffer[n] = 0;

            // Lấy IP
            char *ip = inet_ntoa(client_addr.sin_addr);

            // Lấy thời gian
            time_t t = time(NULL);
            struct tm *tm = localtime(&t);
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);

            // In ra màn hình
            printf("%s %s %s\n", ip, time_str, buffer);

            // Ghi file
            FILE *f = fopen(logfile, "a");
            fprintf(f, "%s %s %s\n", ip, time_str, buffer);
            fclose(f);
        }

        close(client);
    }

    close(server);
    return 0;
}