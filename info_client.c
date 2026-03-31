#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>

void remove_newline(char *s) {
    s[strcspn(s, "\n")] = 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <IP> <PORT>\n", argv[0]);
        return 1;
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);

    // 1. Tạo socket
    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        perror("socket failed");
        return 1;
    }

    // 2. Khai báo địa chỉ server
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    // 3. Kết nối
    if (connect(client, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect failed");
        close(client);
        return 1;
    }

    printf("Connected to server %s:%d\n", ip, port);

    // 4. Mở thư mục và đếm số file
    DIR *dir = opendir(".");
    if (dir == NULL) {
        perror("opendir failed");
        close(client);
        return 1;
    }

    // 5. Đếm số file
    int file_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            file_count++;
        }
    }
    rewinddir(dir);

    // 6. Gửi số file (int - 4 bytes)
    send(client, &file_count, sizeof(int), 0);

    // 7. Gửi dữ liệu file
    char data[2048] = "";
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            struct stat file_stat;
            if (stat(entry->d_name, &file_stat) == 0) {
                char file_info[512];
                snprintf(file_info, sizeof(file_info), "%s %ld\n", entry->d_name, file_stat.st_size);
                if (strlen(data) + strlen(file_info) < sizeof(data)) {
                    strcat(data, file_info);
                }
            }
        }
    }
    closedir(dir);

    // 8. Gửi danh sách file
    send(client, data, strlen(data), 0);
    printf("Data sent!\n");

    // 8. Đóng
    close(client);
    return 0;
}
