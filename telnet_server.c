#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORT 9001
#define BUF_SIZE 2048

static int check_login(const char *user, const char *pass) {
    FILE *f = fopen("users.txt", "r");
    if (!f) {
        return 0;
    }

    char u[50], p[50];
    while (fscanf(f, "%49s %49s", u, p) != EOF) {
        if (strcmp(u, user) == 0 && strcmp(p, pass) == 0) {
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}

static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
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

typedef struct {
    int fd;
} ClientArg;

static void *client_thread(void *arg) {
    ClientArg *ca = (ClientArg *)arg;
    int client_fd = ca->fd;
    free(ca);
    pthread_detach(pthread_self());

    (void)send_all(client_fd, "Welcome to Telnet Server\n", 25);
    (void)send_all(client_fd, "Login format: username password\n", 32);

    char line[BUF_SIZE];
    ssize_t len = recv_line(client_fd, line, sizeof(line));
    if (len <= 0) {
        close(client_fd);
        return NULL;
    }

    char user[50], pass[50];
    if (sscanf(line, "%49s %49s", user, pass) != 2 || !check_login(user, pass)) {
        (void)send_all(client_fd, "Login Failed\n", 13);
        close(client_fd);
        return NULL;
    }

    (void)send_all(client_fd, "Login OK\n", 9);

    while (1) {
        (void)send_all(client_fd, "$ ", 2);
        len = recv_line(client_fd, line, sizeof(line));
        if (len <= 0) {
            break;
        }

        if (strcmp(line, "exit") == 0) {
            (void)send_all(client_fd, "Goodbye\n", 8);
            break;
        }

        if (line[0] == '\0') {
            continue;
        }

        char cmd[BUF_SIZE + 20];
        snprintf(cmd, sizeof(cmd), "%s 2>&1", line);
        FILE *fp = popen(cmd, "r");
        if (fp == NULL) {
            (void)send_all(client_fd, "Cannot execute command\n", 23);
            continue;
        }

        char output[BUF_SIZE];
        while (fgets(output, sizeof(output), fp) != NULL) {
            if (send_all(client_fd, output, strlen(output)) < 0) {
                break;
            }
        }

        pclose(fp);
    }

    close(client_fd);
    return NULL;
}

int main() {
    signal(SIGPIPE, SIG_IGN);

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listener);
        exit(1);
    }

    if (listen(listener, 64) < 0) {
        perror("listen");
        close(listener);
        exit(1);
    }

    printf("Telnet multithread server running on port %d...\n", PORT);

    while (1) {
        int client_fd = accept(listener, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        ClientArg *ca = (ClientArg *)malloc(sizeof(*ca));
        if (!ca) {
            close(client_fd);
            continue;
        }
        ca->fd = client_fd;

        pthread_t th;
        if (pthread_create(&th, NULL, client_thread, ca) != 0) {
            free(ca);
            close(client_fd);
            continue;
        }
    }
}