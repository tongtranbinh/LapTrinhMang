#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define BUF_SIZE 1024

static void remove_newline(char *s) {
    size_t len = strlen(s);
    if (len > 0 && s[len - 1] == '\n') {
        s[len - 1] = '\0';
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <port> <remote_ip> <remote_port>\n", argv[0]);
        return 1;
    }

    int local_port = atoi(argv[1]);
    const char *remote_ip = argv[2];
    int remote_port = atoi(argv[3]);

    if (local_port <= 0 || local_port > 65535 || remote_port <= 0 || remote_port > 65535) {
        printf("Port khong hop le.\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons((unsigned short)local_port);

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons((unsigned short)remote_port);

    if (inet_pton(AF_INET, remote_ip, &remote_addr.sin_addr) != 1) {
        printf("Dia chi IP khong hop le.\n");
        close(sock);
        return 1;
    }

    printf("UDP chat started.\n");


    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);

        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;
        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char send_buf[BUF_SIZE];

            if (fgets(send_buf, sizeof(send_buf), stdin) == NULL) {
                break;
            }

            remove_newline(send_buf);

            if (strcmp(send_buf, "exit") == 0) {
                break;
            }

            if (send_buf[0] != '\0') {
                ssize_t sent = sendto(sock,
                                      send_buf,
                                      strlen(send_buf),
                                      0,
                                      (struct sockaddr *)&remote_addr,
                                      sizeof(remote_addr));
                if (sent < 0) {
                    perror("sendto");
                }
            }
        }

        if (FD_ISSET(sock, &readfds)) {
            char recv_buf[BUF_SIZE];
            struct sockaddr_in sender_addr;
            socklen_t sender_len = sizeof(sender_addr);

            ssize_t len = recvfrom(sock,
                                   recv_buf,
                                   sizeof(recv_buf) - 1,
                                   0,
                                   (struct sockaddr *)&sender_addr,
                                   &sender_len);

            if (len < 0) {
                perror("recvfrom");
                continue;
            }

            recv_buf[len] = '\0';

            char sender_ip[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip)) == NULL) {
                strcpy(sender_ip, "unknown");
            }

            printf("\n[%s:%d] %s\n",
                   sender_ip,
                   ntohs(sender_addr.sin_port),
                   recv_buf);
            fflush(stdout);
        }
    }

    close(sock);
    return 0;
}