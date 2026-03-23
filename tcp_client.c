#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    // Kiểm tra tham số dòng lệnh
    if (argc != 3) {
        printf("Usage: %s <IP> <PORT>\n", argv[0]);
        return 1;
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);

    // 1. Tạo socket
    int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == -1) {
        perror("socket failed");
        return 1;
    }

    // 2. Khai báo địa chỉ server
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    // 3. Kết nối đến server
    if (connect(client, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        close(client);
        return 1;
    }

    printf("Connected to server %s:%d\n", ip, port);

    char welcome[1024];

    // nhận dữ liệu từ server (lời chào)
    int n = recv(client, welcome, sizeof(welcome)-1, 0);
    if (n > 0) {
        welcome[n] = 0;
        printf("Server: %s\n", welcome );
    }

    // 4. Nhập dữ liệu từ bàn phím và gửi
    char buffer[1024];
    while (1) {
        printf("Enter message: ");
        fgets(buffer, sizeof(buffer), stdin);

        // gửi dữ liệu
        send(client, buffer, strlen(buffer), 0);

        // thoát nếu nhập exit
        if (strncmp(buffer, "exit", 4) == 0)
            break;
    }

    // 5. Đóng socket
    close(client);
    return 0;
}