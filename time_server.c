#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUF_SIZE 2048

static void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
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

static const char *format_to_strftime(const char *fmt) {
    if (strcmp(fmt, "dd/mm/yyyy") == 0) {
        return "%d/%m/%Y";
    }
    if (strcmp(fmt, "dd/mm/yy") == 0) {
        return "%d/%m/%y";
    }
    if (strcmp(fmt, "mm/dd/yyyy") == 0) {
        return "%m/%d/%Y";
    }
    if (strcmp(fmt, "mm/dd/yy") == 0) {
        return "%m/%d/%y";
    }
    return NULL;
}

static void send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        sent += (size_t)n;
    }
}

static void send_error(int fd, const char *msg) {
    send_all(fd, "ERROR ", 6);
    send_all(fd, msg, strlen(msg));
    send_all(fd, "\n", 1);
}

static void handle_client(int cfd) {
    const char *hello =
        "time_server ready. Commands:\n"
        "  GET_TIME [dd/mm/yyyy|dd/mm/yy|mm/dd/yyyy|mm/dd/yy]\n"
        "  quit\n";
    send_all(cfd, hello, strlen(hello));

    while (1) {
        char line[BUF_SIZE];
        ssize_t n = recv_line(cfd, line, sizeof(line));
        if (n <= 0) {
            return;
        }

        if (strlen(line) == 0) {
            continue;
        }

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            return;
        }

        // Tokenize: GET_TIME [format]
        char cmd[64] = {0};
        char fmt[64] = {0};
        char extra[64] = {0};
        int parts = sscanf(line, "%63s %63s %63s", cmd, fmt, extra);

        if (parts < 1) {
            send_error(cfd, "Empty command");
            continue;
        }

        if (strcmp(cmd, "GET_TIME") != 0) {
            send_error(cfd, "Unknown command. Use GET_TIME [format]"
                             );
            continue;
        }

        if (parts == 3) {
            send_error(cfd, "Too many arguments. Use GET_TIME [format]");
            continue;
        }

        const char *strftime_fmt = "%d/%m/%Y"; // default
        if (parts == 2) {
            const char *mapped = format_to_strftime(fmt);
            if (mapped == NULL) {
                send_error(cfd, "Invalid format. Supported: dd/mm/yyyy, dd/mm/yy, mm/dd/yyyy, mm/dd/yy");
                continue;
            }
            strftime_fmt = mapped;
        }

        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);

        char out[128];
        size_t out_len = strftime(out, sizeof(out), strftime_fmt, &tm_now);
        if (out_len == 0) {
            send_error(cfd, "Internal error formatting time");
            continue;
        }

        send_all(cfd, out, out_len);
        send_all(cfd, "\n", 1);
    }
}

typedef struct {
    int fd;
} ClientArg;

static void *client_thread(void *arg) {
    ClientArg *ca = (ClientArg *)arg;
    int cfd = ca->fd;
    free(ca);
    pthread_detach(pthread_self());

    handle_client(cfd);
    close(cfd);
    return NULL;
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
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listener);
        return 1;
    }

    if (listen(listener, 64) < 0) {
        perror("listen");
        close(listener);
        return 1;
    }

    printf("time_server (multithread) listening on port %d\n", port);

    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(listener, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        ClientArg *ca = (ClientArg *)malloc(sizeof(*ca));
        if (!ca) {
            close(cfd);
            continue;
        }
        ca->fd = cfd;

        pthread_t th;
        if (pthread_create(&th, NULL, client_thread, ca) != 0) {
            free(ca);
            close(cfd);
            continue;
        }
    }
}
