#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>

#define BUF_SIZE 1024

void remove_newline(char *s) {
    size_t len = strlen(s);
    if (len > 0 && s[len - 1] == '\n') {
        s[len - 1] = '\0';
    }
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <port_s> <ip_d> <port_d>\n", argv[0]);
        return 1;
    }

    int port_s = atoi(argv[1]);
    char *ip_d = argv[2];
    int port_d = atoi(argv[3]);

    if (port_s <= 0 || port_s > 65535 || port_d <= 0 || port_d > 65535) {
        printf("Port khong hop le.\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Gắn cổng nhận của ứng dụng hiện tại
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(port_s);

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    // Địa chỉ đích để gửi
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port_d);

    if (inet_pton(AF_INET, ip_d, &dest_addr.sin_addr) != 1) {
        printf("Dia chi IP khong hop le.\n");
        close(sock);
        return 1;
    }

    // Chuyển socket sang non-blocking
    if (set_nonblocking(sock) < 0) {
        perror("fcntl socket");
        close(sock);
        return 1;
    }

    // Chuyển stdin sang non-blocking để vừa gõ vừa nhận
    if (set_nonblocking(STDIN_FILENO) < 0) {
        perror("fcntl stdin");
        close(sock);
        return 1;
    }

    printf("UDP chat started.\n");
    printf("Nhan tai port: %d\n", port_s);
    printf("Gui den: %s:%d\n", ip_d, port_d);
    printf("Nhap 'exit' de thoat.\n");

    char send_buf[BUF_SIZE];
    char recv_buf[BUF_SIZE];
    char stdin_buf[BUF_SIZE];

    while (1) {
        // 1. Thử đọc từ bàn phím
        memset(stdin_buf, 0, sizeof(stdin_buf));
        if (fgets(stdin_buf, sizeof(stdin_buf), stdin) != NULL) {
            remove_newline(stdin_buf);

            if (strcmp(stdin_buf, "exit") == 0) {
                break;
            }

            if (strlen(stdin_buf) > 0) {
                int sent = sendto(sock,
                                  stdin_buf,
                                  strlen(stdin_buf),
                                  0,
                                  (struct sockaddr *)&dest_addr,
                                  sizeof(dest_addr));
                if (sent < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("sendto");
                    }
                }
            }
        } else {
            // Nếu không có dữ liệu từ bàn phím thì stdin non-blocking sẽ trả về NULL
            // không cần coi là lỗi thật
            clearerr(stdin);
        }

        // 2. Thử nhận dữ liệu từ socket
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        memset(recv_buf, 0, sizeof(recv_buf));

        int ret = recvfrom(sock,
                           recv_buf,
                           sizeof(recv_buf) - 1,
                           0,
                           (struct sockaddr *)&sender_addr,
                           &sender_len);

        if (ret > 0) {
            recv_buf[ret] = '\0';

            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip));

            printf("\n[%s:%d] %s\n",
                   sender_ip,
                   ntohs(sender_addr.sin_port),
                   recv_buf);
            fflush(stdout);
        } else if (ret < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("recvfrom");
            }
        }

        // Tránh busy waiting ăn CPU 100%
        usleep(10000); // 10 ms
    }

    close(sock);
    return 0;
}