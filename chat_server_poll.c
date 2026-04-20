#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>

#define MAX_CLIENTS 100
#define BUF_SIZE 2048
#define ID_SIZE 50
#define NAME_SIZE 50

struct Client {
    int fd;
    char id[ID_SIZE];
    int registered;
};

struct Client clients[MAX_CLIENTS];

void remove_client(struct pollfd *fds, int *nfds, int idx) {
    close(fds[idx].fd);
    fds[idx] = fds[*nfds - 1];
    clients[idx] = clients[*nfds - 1];
    (*nfds)--;
}

void broadcast(int sender, const char *msg, int nfds, struct pollfd *fds) {
    for (int i = 1; i < nfds; i++) {
        if (fds[i].fd != sender) {
            send(fds[i].fd, msg, strlen(msg), 0);
        }
    }
}

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listener);
        return 1;
    }

    if (listen(listener, 5) < 0) {
        perror("listen");
        close(listener);
        return 1;
    }

    struct pollfd fds[MAX_CLIENTS];
    int nfds = 1;

    fds[0].fd = listener;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    memset(clients, 0, sizeof(clients));

    printf("Chat server running on port 9000...\n");

    while (1) {
        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            int client = accept(listener, NULL, NULL);
            if (client < 0) {
                perror("accept");
            } else if (nfds >= MAX_CLIENTS) {
                const char *full_msg = "Server full\n";
                send(client, full_msg, strlen(full_msg), 0);
                close(client);
            } else {
                printf("New client: %d\n", client);

                fds[nfds].fd = client;
                fds[nfds].events = POLLIN;
                fds[nfds].revents = 0;

                clients[nfds].fd = client;
                clients[nfds].registered = 0;
                clients[nfds].id[0] = '\0';

                send(client, "Nhap theo dang: client_id: client_name\n",
                     strlen("Nhap theo dang: client_id: client_name\n"), 0);

                nfds++;
            }
        }

        for (int i = 1; i < nfds; i++) {
            if (fds[i].revents & (POLLIN | POLLERR | POLLHUP)) {
                char buf[BUF_SIZE];
                int len = recv(fds[i].fd, buf, sizeof(buf) - 1, 0);

                if (len <= 0) {
                    printf("Client %d disconnected\n", fds[i].fd);
                    remove_client(fds, &nfds, i);
                    i--;
                    continue;
                }

                buf[len] = '\0';

                if (!clients[i].registered) {
                    char id[ID_SIZE];
                    char name[NAME_SIZE];

                    if (sscanf(buf, " %49[^:]: %49s", id, name) == 2) {
                        strcpy(clients[i].id, id);
                        clients[i].registered = 1;
                        send(fds[i].fd, "OK\n", 3, 0);
                        printf("Client %d registered with id = %s\n", fds[i].fd, clients[i].id);
                    } else {
                        send(fds[i].fd, "Sai cu phap. Dung: client_id: client_name\n",
                             strlen("Sai cu phap. Dung: client_id: client_name\n"), 0);
                    }
                    continue;
                }

                char msg[BUF_SIZE];
                time_t t = time(NULL);
                struct tm *tm_info = localtime(&t);
                char timebuf[64];

                strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);

                int prefix_len = snprintf(msg, sizeof(msg), "%s %s: ",
                                          timebuf, clients[i].id);

                if (prefix_len < 0 || prefix_len >= (int)sizeof(msg)) {
                    continue;
                }

                int remain = sizeof(msg) - prefix_len;

                snprintf(msg + prefix_len, remain, "%.*s",
                         remain - 1, buf);

                broadcast(fds[i].fd, msg, nfds, fds);
            }
        }
    }

    close(listener);
    return 0;
}