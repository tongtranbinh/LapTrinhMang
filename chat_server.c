#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 100
#define BUF_SIZE 1024

typedef struct {
    int fd;
    int is_logged_in;
    char client_id[64];
    char client_name[64];
} Client;

static void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static int is_single_token(const char *s) {
    if (s == NULL || *s == '\0') {
        return 0;
    }

    for (size_t i = 0; s[i] != '\0'; i++) {
        if (isspace((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static int parse_login(const char *line, char *out_id, size_t out_id_sz,
                       char *out_name, size_t out_name_sz) {
    const char *colon = strchr(line, ':');
    if (colon == NULL || strchr(colon + 1, ':') != NULL) {
        return 0;
    }

    size_t id_len = (size_t)(colon - line);
    if (id_len == 0 || id_len >= out_id_sz) {
        return 0;
    }

    char id_buf[64];
    memcpy(id_buf, line, id_len);
    id_buf[id_len] = '\0';

    while (id_len > 0 && isspace((unsigned char)id_buf[id_len - 1])) {
        id_buf[id_len - 1] = '\0';
        id_len--;
    }
    if (!is_single_token(id_buf)) {
        return 0;
    }

    const char *name_start = colon + 1;
    while (*name_start != '\0' && isspace((unsigned char)*name_start)) {
        name_start++;
    }
    if (*name_start == '\0') {
        return 0;
    }

    size_t name_len = strlen(name_start);
    while (name_len > 0 && isspace((unsigned char)name_start[name_len - 1])) {
        name_len--;
    }
    if (name_len == 0 || name_len >= out_name_sz) {
        return 0;
    }

    char name_buf[64];
    memcpy(name_buf, name_start, name_len);
    name_buf[name_len] = '\0';
    if (!is_single_token(name_buf)) {
        return 0;
    }

    snprintf(out_id, out_id_sz, "%s", id_buf);
    snprintf(out_name, out_name_sz, "%s", name_buf);
    return 1;
}

static void broadcast_message(Client clients[], int sender_fd, const char *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd > 0 && clients[i].fd != sender_fd && clients[i].is_logged_in) {
            send(clients[i].fd, msg, strlen(msg), 0);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    Client clients[MAX_CLIENTS];
    memset(clients, 0, sizeof(clients));

    printf("Chat server listening on port %d...\n", port);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);

        int max_fd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = clients[i].fd;
            if (fd > 0) {
                FD_SET(fd, &readfds);
                if (fd > max_fd) {
                    max_fd = fd;
                }
            }
        }

        int ready = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        if (FD_ISSET(server_fd, &readfds)) {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int client_fd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
            if (client_fd < 0) {
                perror("accept");
            } else {
                int added = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == 0) {
                        clients[i].fd = client_fd;
                        clients[i].is_logged_in = 0;
                        clients[i].client_id[0] = '\0';
                        clients[i].client_name[0] = '\0';
                        added = 1;
                        break;
                    }
                }

                if (!added) {
                    const char *full = "Server full. Try again later.\n";
                    send(client_fd, full, strlen(full), 0);
                    close(client_fd);
                } else {
                    const char *welcome =
                        "Welcome. Login with: client_id: client_name\n"
                        "Example: B21DCCN001: an\n";
                    send(client_fd, welcome, strlen(welcome), 0);
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = clients[i].fd;
            if (fd <= 0 || !FD_ISSET(fd, &readfds)) {
                continue;
            }

            char buf[BUF_SIZE + 1];
            int n = recv(fd, buf, BUF_SIZE, 0);
            if (n <= 0) {
                struct sockaddr_in peer;
                socklen_t plen = sizeof(peer);
                char leave_msg[256] = "[Server] A client left chat\n";
                if (getpeername(fd, (struct sockaddr *)&peer, &plen) == 0) {
                    snprintf(leave_msg, sizeof(leave_msg), "[Server] %s:%d left chat\n",
                             inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
                }
                printf("%s", leave_msg);
                if (clients[i].is_logged_in) {
                    broadcast_message(clients, fd, leave_msg);
                }
                close(fd);
                clients[i].fd = 0;
                clients[i].is_logged_in = 0;
                clients[i].client_id[0] = '\0';
                clients[i].client_name[0] = '\0';
                continue;
            }

            buf[n] = '\0';
            trim_newline(buf);

            if (!clients[i].is_logged_in) {
                char parsed_id[64];
                char parsed_name[64];
                if (!parse_login(buf, parsed_id, sizeof(parsed_id), parsed_name,
                                 sizeof(parsed_name))) {
                    const char *invalid =
                        "Invalid format. Use: client_id: client_name\n"
                        "client_name must be one word.\n";
                    send(fd, invalid, strlen(invalid), 0);
                    continue;
                }

                clients[i].is_logged_in = 1;
                snprintf(clients[i].client_id, sizeof(clients[i].client_id), "%s", parsed_id);
                snprintf(clients[i].client_name, sizeof(clients[i].client_name), "%s", parsed_name);

                char ok_msg[256];
                snprintf(ok_msg, sizeof(ok_msg),
                         "Login ok. Hello %s (%s). Type /quit to leave.\n",
                         clients[i].client_name, clients[i].client_id);
                send(fd, ok_msg, strlen(ok_msg), 0);

                char join_msg[256];
                snprintf(join_msg, sizeof(join_msg), "[Server] %s (%s) joined chat\n",
                         clients[i].client_name, clients[i].client_id);
                printf("%s", join_msg);
                broadcast_message(clients, fd, join_msg);
                continue;
            }

            if (strcmp(buf, "/quit") == 0) {
                const char *bye = "Bye.\n";
                send(fd, bye, strlen(bye), 0);
                close(fd);
                char leave_msg[256];
                snprintf(leave_msg, sizeof(leave_msg), "[Server] %s (%s) left chat\n",
                         clients[i].client_name, clients[i].client_id);
                printf("%s", leave_msg);
                broadcast_message(clients, fd, leave_msg);

                clients[i].fd = 0;
                clients[i].is_logged_in = 0;
                clients[i].client_id[0] = '\0';
                clients[i].client_name[0] = '\0';
                continue;
            }

            char out[BUF_SIZE + 256];
            snprintf(out, sizeof(out), "[%s|%s] %s\n", clients[i].client_id,
                     clients[i].client_name, buf);

            printf("%s", out);
            broadcast_message(clients, fd, out);
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd > 0) {
            close(clients[i].fd);
        }
    }
    close(server_fd);
    return 0;
}
