#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static Client g_clients[MAX_CLIENTS];
static pthread_mutex_t g_clients_lock = PTHREAD_MUTEX_INITIALIZER;

static void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static ssize_t send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

static ssize_t recv_line(int fd, char *buf, size_t cap) {
    if (cap == 0) {
        return -1;
    }

    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }
        buf[i++] = c;
    }

    buf[i] = '\0';
    trim_newline(buf);
    return (ssize_t)i;
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

static void broadcast_message(int sender_fd, const char *msg) {
    pthread_mutex_lock(&g_clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd > 0 && g_clients[i].fd != sender_fd && g_clients[i].is_logged_in) {
            (void)send_all(g_clients[i].fd, msg, strlen(msg));
        }
    }
    pthread_mutex_unlock(&g_clients_lock);
}

static void remove_client_fd(int fd) {
    pthread_mutex_lock(&g_clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].fd == fd) {
            g_clients[i].fd = 0;
            g_clients[i].is_logged_in = 0;
            g_clients[i].client_id[0] = '\0';
            g_clients[i].client_name[0] = '\0';
            break;
        }
    }
    pthread_mutex_unlock(&g_clients_lock);
}

typedef struct {
    int fd;
} ClientThreadArg;

static void *client_thread(void *arg) {
    ClientThreadArg *cta = (ClientThreadArg *)arg;
    int fd = cta->fd;
    free(cta);
    pthread_detach(pthread_self());

    const char *welcome =
        "Welcome. Login with: client_id: client_name\n"
        "Example: B21DCCN001: an\n";
    (void)send_all(fd, welcome, strlen(welcome));

    int logged_in = 0;
    char client_id[64] = {0};
    char client_name[64] = {0};

    while (1) {
        char line[BUF_SIZE + 1];
        ssize_t n = recv_line(fd, line, sizeof(line));
        if (n <= 0) {
            break;
        }

        if (!logged_in) {
            char parsed_id[64];
            char parsed_name[64];
            if (!parse_login(line, parsed_id, sizeof(parsed_id), parsed_name, sizeof(parsed_name))) {
                const char *invalid =
                    "Invalid format. Use: client_id: client_name\n"
                    "client_name must be one word.\n";
                (void)send_all(fd, invalid, strlen(invalid));
                continue;
            }

            snprintf(client_id, sizeof(client_id), "%s", parsed_id);
            snprintf(client_name, sizeof(client_name), "%s", parsed_name);
            logged_in = 1;

            pthread_mutex_lock(&g_clients_lock);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (g_clients[i].fd == fd) {
                    g_clients[i].is_logged_in = 1;
                    snprintf(g_clients[i].client_id, sizeof(g_clients[i].client_id), "%s", client_id);
                    snprintf(g_clients[i].client_name, sizeof(g_clients[i].client_name), "%s", client_name);
                    break;
                }
            }
            pthread_mutex_unlock(&g_clients_lock);

            char ok_msg[256];
            snprintf(ok_msg, sizeof(ok_msg),
                     "Login ok. Hello %s (%s). Type /quit to leave.\n",
                     client_name, client_id);
            (void)send_all(fd, ok_msg, strlen(ok_msg));

            char join_msg[256];
            snprintf(join_msg, sizeof(join_msg), "[Server] %s (%s) joined chat\n",
                     client_name, client_id);
            printf("%s", join_msg);
            broadcast_message(fd, join_msg);
            continue;
        }

        if (strcmp(line, "/quit") == 0) {
            const char *bye = "Bye.\n";
            (void)send_all(fd, bye, strlen(bye));
            break;
        }

        char out[BUF_SIZE + 256];
        snprintf(out, sizeof(out), "[%s|%s] %s\n", client_id, client_name, line);
        printf("%s", out);
        broadcast_message(fd, out);
    }

    if (logged_in) {
        char leave_msg[256];
        snprintf(leave_msg, sizeof(leave_msg), "[Server] %s (%s) left chat\n", client_name, client_id);
        printf("%s", leave_msg);
        broadcast_message(fd, leave_msg);
    }

    remove_client_fd(fd);
    close(fd);
    return NULL;
}

static int create_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 64) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
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

    signal(SIGPIPE, SIG_IGN);
    memset(g_clients, 0, sizeof(g_clients));

    int server_fd = create_server_socket(port);
    if (server_fd < 0) {
        return 1;
    }

    printf("Chat server (multithread) listening on port %d...\n", port);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        int added = 0;
        pthread_mutex_lock(&g_clients_lock);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].fd == 0) {
                g_clients[i].fd = client_fd;
                g_clients[i].is_logged_in = 0;
                g_clients[i].client_id[0] = '\0';
                g_clients[i].client_name[0] = '\0';
                added = 1;
                break;
            }
        }
        pthread_mutex_unlock(&g_clients_lock);

        if (!added) {
            const char *full = "Server full. Try again later.\n";
            (void)send_all(client_fd, full, strlen(full));
            close(client_fd);
            continue;
        }

        ClientThreadArg *cta = (ClientThreadArg *)malloc(sizeof(*cta));
        if (!cta) {
            const char *busy = "Server busy. Try again later.\n";
            (void)send_all(client_fd, busy, strlen(busy));
            remove_client_fd(client_fd);
            close(client_fd);
            continue;
        }
        cta->fd = client_fd;

        pthread_t th;
        if (pthread_create(&th, NULL, client_thread, cta) != 0) {
            const char *busy = "Server busy. Try again later.\n";
            (void)send_all(client_fd, busy, strlen(busy));
            free(cta);
            remove_client_fd(client_fd);
            close(client_fd);
            continue;
        }
    }
}
