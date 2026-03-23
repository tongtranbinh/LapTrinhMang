#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

// Hàm xóa '\n'
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

    // 4. Nhập dữ liệu
    char mssv[50], name[100], dob[50], gpa[20];

    printf("MSSV: ");
    fgets(mssv, sizeof(mssv), stdin);
    remove_newline(mssv);

    printf("Ho ten: ");
    fgets(name, sizeof(name), stdin);
    remove_newline(name);

    printf("Ngay sinh: ");
    fgets(dob, sizeof(dob), stdin);
    remove_newline(dob);

    printf("Diem TB: ");
    fgets(gpa, sizeof(gpa), stdin);
    remove_newline(gpa);

    // 5. Ghép chuỗi (format đẹp hơn)
    char data[256];
    sprintf(data, "%s %s %s %s", mssv, name, dob, gpa);

    // 6. Gửi
    send(client, data, strlen(data), 0);

    printf("Data sent!\n");

    // 7. Đóng
    close(client);
    return 0;
}