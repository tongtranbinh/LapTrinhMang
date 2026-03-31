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

    // 1. Tạo socket server
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket failed");
        return 1;
    }

    // 2. Khai báo địa chỉ server
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    // 3. Bind
    if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(server);
        return 1;
    }

    // 4. Listen
    listen(server, 1);
    printf("Server listening on port %d\n", port);

    // 5. Accept kết nối
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    int client = accept(server, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
    
    if (client < 0) {
        perror("accept failed");
        close(server);
        return 1;
    }

    printf("Client connected from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // 6. Nhận số file
    int file_count = 0;
    recv(client, &file_count, sizeof(int), 0);

    printf("Number of files: %d\n\n", file_count);

    // 7. Nhận và xử lý danh sách file
    char buffer[2048] = "";
    recv(client, buffer, sizeof(buffer) - 1, 0);
    buffer[sizeof(buffer) - 1] = '\0';

    // 8. Tách dữ liệu và in ra màn hình
    printf("File List:\n");
    char *line = strtok(buffer, "\n");
    while (line != NULL && strlen(line) > 0) {
        char filename[256];
        long size;
        sscanf(line, "%s %ld", filename, &size);
        printf("  %s - %ld bytes\n", filename, size);
        line = strtok(NULL, "\n");
    }

    // 8. Đóng
    close(client);
    close(server);
    return 0;
}
