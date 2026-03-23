#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <port> <welcome_file> <output_file>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    char *welcome_file = argv[2];
    char *output_file = argv[3];

    // 1. Tạo socket
    int server = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    // 2. Bind
    bind(server, (struct sockaddr*)&addr, sizeof(addr));

    // 3. Listen
    listen(server, 5);
    printf("Server listening on port %d...\n", port);

    // 4. Accept
    int client = accept(server, NULL, NULL);
    printf("Client connected!\n");

    // 5. Gửi lời chào từ file
    FILE *f = fopen(welcome_file, "r");
    char buffer[1024];
    int len = fread(buffer, 1, sizeof(buffer), f);
    send(client, buffer, len, 0);
    fclose(f);

    // 6. Nhận dữ liệu và ghi file
    FILE *out = fopen(output_file, "w");
    while (1) {
        int n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        fwrite(buffer, 1, n, out);
    }

    fclose(out);

    close(client);
    close(server);
    return 0;
}