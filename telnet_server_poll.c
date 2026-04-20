#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>

#define MAX_CLIENTS 100
#define BUF_SIZE 2048

struct Client {
    int fd;
    int logged;
};

struct Client clients[MAX_CLIENTS];

int check_login(char *user, char *pass) {
    FILE *f = fopen("users.txt", "r");
    if (!f) return 0;

    char u[50], p[50];

    while (fscanf(f, "%s %s", u, p) != EOF) {
        if (strcmp(u, user) == 0 && strcmp(p, pass) == 0) {
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9001);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(listener, (struct sockaddr*)&addr, sizeof(addr));
    listen(listener, 5);

    struct pollfd fds[MAX_CLIENTS];
    int nfds = 1;

    fds[0].fd = listener;
    fds[0].events = POLLIN;

    printf("Telnet server running...\n");

    while (1) {
        poll(fds, nfds, -1);

        if (fds[0].revents & POLLIN) {
            int client = accept(listener, NULL, NULL);

            fds[nfds].fd = client;
            fds[nfds].events = POLLIN;

            clients[nfds].fd = client;
            clients[nfds].logged = 0;

            nfds++;
        }

        for (int i = 1; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                char buf[BUF_SIZE];
                int len = recv(fds[i].fd, buf, sizeof(buf), 0);

                if (len <= 0) {
                    close(fds[i].fd);
                    fds[i] = fds[nfds - 1];
                    clients[i] = clients[nfds - 1];
                    nfds--;
                    i--;
                    continue;
                }

                buf[len] = 0;

                if (!clients[i].logged) {
                    char user[50], pass[50];

                    if (sscanf(buf, "%s %s", user, pass) == 2 &&
                        check_login(user, pass)) {
                        clients[i].logged = 1;
                        send(fds[i].fd, "Login OK\n", 9, 0);
                    } else {
                        send(fds[i].fd, "Login Failed\n", 13, 0);
                    }
                    continue;
                }

                // thực hiện lệnh
                char cmd[BUF_SIZE + 20];
                snprintf(cmd, sizeof(cmd), "%s > out.txt", buf);
                system(cmd);

                FILE *f = fopen("out.txt", "r");
                char out[BUF_SIZE];

                while (fgets(out, sizeof(out), f)) {
                    send(fds[i].fd, out, strlen(out), 0);
                }

                fclose(f);
            }
        }
    }
}