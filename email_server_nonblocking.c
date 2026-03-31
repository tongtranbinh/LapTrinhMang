#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_CLIENTS 100
#define MAX_NAME_LEN 100
#define MAX_MSSV_LEN 50

struct Client {
    int sock;
    int stage;
    char name[MAX_NAME_LEN];
    char mssv[MAX_MSSV_LEN];
};

struct Client clients[MAX_CLIENTS];
int numClients = 0;

void set_nonblocking(int sock) {
    fcntl(sock, F_SETFL, O_NONBLOCK);
}

void removeClient(int i) {
    close(clients[i].sock);
    clients[i] = clients[numClients - 1];
    numClients--;
}

#include <stdio.h>
#include <string.h>
#include <ctype.h>

void remove_newline(char *s) {
    s[strcspn(s, "\n")] = 0;
}

static void copy_bounded(char *dst, size_t dst_sz, const char *src) {
    if (dst_sz == 0) return;
    size_t n = strnlen(src, dst_sz - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void make_hust_email(const char *fullname, const char *mssv, char *email, size_t email_sz) {
    char name[MAX_NAME_LEN];
    snprintf(name, sizeof(name), "%s", fullname);

    char *parts[20];
    int count = 0;

    char *token = strtok(name, " ");
    while (token != NULL && count < 20) {
        parts[count++] = token;
        token = strtok(NULL, " ");
    }

    if (count == 0) {
        snprintf(email, email_sz, "%s", "invalid@sis.hust.edu.vn");
        return;
    }

    // Tên = từ cuối
    char last_name[50];
    snprintf(last_name, sizeof(last_name), "%s", parts[count - 1]);

    // Đổi tên về chữ thường
    for (int i = 0; last_name[i]; i++) {
        last_name[i] = tolower((unsigned char)last_name[i]);
    }

    // Lấy chữ cái đầu của họ + tên đệm
    char initials[20] = "";
    for (int i = 0; i < count - 1; i++) {
        int len = strlen(initials);
        initials[len] = tolower((unsigned char)parts[i][0]);
        initials[len + 1] = '\0';
    }

    // Lấy 6 số cuối MSSV
    int len_mssv = strlen(mssv);
    const char *last6 = (len_mssv >= 6) ? (mssv + len_mssv - 6) : mssv;

    snprintf(email, email_sz, "%s.%s%s@sis.hust.edu.vn", last_name, initials, last6);
}

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(listener, (struct sockaddr*)&addr, sizeof(addr));
    listen(listener, 5);

    set_nonblocking(listener);

    printf("Server started...\n");

    while (1) {
        // Accept client
        int client = accept(listener, NULL, NULL);
        if (client != -1) {
            printf("New client connected\n");

            set_nonblocking(client);

            clients[numClients].sock = client;
            clients[numClients].stage = 0;
            numClients++;

            send(client, "Nhap Ho ten:\n", 14, 0);
        }

        // Handle clients
        for (int i = 0; i < numClients; i++) {
            char buf[256];
            int ret = recv(clients[i].sock, buf, sizeof(buf) - 1, 0);

            if (ret == -1) {
                if (errno != EWOULDBLOCK) {
                    removeClient(i);
                    i--;
                }
                continue;
            }

            if (ret == 0) {
                removeClient(i);
                i--;
                continue;
            }

            buf[ret] = 0;
            buf[strcspn(buf, "\n")] = 0; // remove \n

            if (clients[i].stage == 0) {
                copy_bounded(clients[i].name, sizeof(clients[i].name), buf);
                clients[i].stage = 1;
                send(clients[i].sock, "Nhap MSSV:\n", 13, 0);
            }
            else if (clients[i].stage == 1) {
                copy_bounded(clients[i].mssv, sizeof(clients[i].mssv), buf);

                char email[200];
                make_hust_email(clients[i].name, clients[i].mssv, email, sizeof(email));

                send(clients[i].sock, email, strlen(email), 0);

                // Quay lại stage 0 để hỏi lại
                clients[i].stage = 2;
            }
        }
    }

    return 0;
}