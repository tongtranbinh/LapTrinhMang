#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 100
#define MAX_TOPICS 100
#define MAX_TOPIC_LEN 64
#define BUF_SIZE 1024

// Lưu trạng thái của từng client TCP đang kết nối.
typedef struct {
    int fd;
    char buffer[BUF_SIZE * 2];
    size_t used;
} Client;

// Mỗi topic giữ danh sách các socket đã SUB topic đó.
typedef struct {
    char topic[MAX_TOPIC_LEN];
    int subscribers[MAX_CLIENTS];
} TopicEntry;

// Xóa ký tự xuống dòng để dễ so sánh chuỗi.
static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

// Reset một slot client sau khi client ngắt kết nối.
static void reset_client(Client *client) {
    client->fd = 0;
    client->used = 0;
    client->buffer[0] = '\0';
}

// Tìm topic đã tồn tại trong mảng topics.
static int find_topic_index(TopicEntry topics[], const char *topic) {
    for (int i = 0; i < MAX_TOPICS; i++) {
        if (topics[i].topic[0] != '\0' && strcmp(topics[i].topic, topic) == 0) {
            return i;
        }
    }
    return -1;
}

// Lấy topic nếu đã có, nếu chưa có thì tạo mới một slot trống.
static int get_or_create_topic(TopicEntry topics[], const char *topic) {
    int idx = find_topic_index(topics, topic);
    if (idx >= 0) {
        return idx;
    }

    for (int i = 0; i < MAX_TOPICS; i++) {
        if (topics[i].topic[0] == '\0') {
            snprintf(topics[i].topic, sizeof(topics[i].topic), "%s", topic);
            for (int j = 0; j < MAX_CLIENTS; j++) {
                topics[i].subscribers[j] = 0;
            }
            return i;
        }
    }

    return -1;
}

// Khi client ngắt kết nối thì xóa fd đó khỏi mọi topic đã đăng ký.
static void remove_client_from_topics(TopicEntry topics[], int fd) {
    for (int i = 0; i < MAX_TOPICS; i++) {
        if (topics[i].topic[0] == '\0') {
            continue;
        }
        for (int j = 0; j < MAX_CLIENTS; j++) {
            if (topics[i].subscribers[j] == fd) {
                topics[i].subscribers[j] = 0;
            }
        }
    }
}

// Đăng ký client vào một topic nếu chưa có trong danh sách.
static void subscribe_client(TopicEntry topics[], int fd, const char *topic) {
    int topic_idx = get_or_create_topic(topics, topic);
    if (topic_idx < 0) {
        return;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (topics[topic_idx].subscribers[i] == fd) {
            return;
        }
        if (topics[topic_idx].subscribers[i] == 0) {
            topics[topic_idx].subscribers[i] = fd;
            return;
        }
    }
}

// Hủy đăng ký client khỏi một topic cụ thể.
static void unsubscribe_client(TopicEntry topics[], int fd, const char *topic) {
    int topic_idx = find_topic_index(topics, topic);
    if (topic_idx < 0) {
        return;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (topics[topic_idx].subscribers[i] == fd) {
            topics[topic_idx].subscribers[i] = 0;
            return;
        }
    }
}

// Chuyển tiếp thông điệp PUB đến tất cả client đã SUB topic tương ứng.
static void send_to_subscribers(Client clients[], TopicEntry topics[], const char *topic, const char *msg, int sender_fd) {
    int topic_idx = find_topic_index(topics, topic);
    if (topic_idx < 0) {
        return;
    }

    // Định dạng gói tin gửi đi cho client nhận.
    char packet[BUF_SIZE + MAX_TOPIC_LEN + 16];
    snprintf(packet, sizeof(packet), "[%s] %s\n", topic, msg);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = topics[topic_idx].subscribers[i];
        if (fd > 0) {
            send(fd, packet, strlen(packet), 0);
        }
    }
}

// Đóng socket và dọn dữ liệu của client tại một slot.
static void remove_client(Client clients[], TopicEntry topics[], int idx) {
    int fd = clients[idx].fd;
    if (fd > 0) {
        remove_client_from_topics(topics, fd);
        close(fd);
    }
    reset_client(&clients[idx]);
}

int main(void) {
    // Tạo TCP server và lắng nghe trên cổng 9000.
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listener);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(9000);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listener);
        return 1;
    }

    if (listen(listener, 10) < 0) {
        perror("listen");
        close(listener);
        return 1;
    }

    // Mảng quản lý client đang kết nối và topic đã đăng ký.
    Client clients[MAX_CLIENTS];
    TopicEntry topics[MAX_TOPICS];
    memset(clients, 0, sizeof(clients));
    memset(topics, 0, sizeof(topics));

    printf("Pub/Sub server listening on port 9000...\n");
    printf("Commands: SUB <topic> | UNSUB <topic> | PUB <topic> <msg>\n");

    // Vòng lặp chính: chờ accept mới hoặc nhận dữ liệu từ client.
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listener, &readfds);

        // Đưa tất cả socket client đang mở vào tập chờ đọc.
        int max_fd = listener;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd > 0) {
                FD_SET(clients[i].fd, &readfds);
                if (clients[i].fd > max_fd) {
                    max_fd = clients[i].fd;
                }
            }
        }

        // select() chờ cho đến khi có socket sẵn sàng đọc.
        int ready = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        // Có client mới kết nối thì accept và cấp một slot trong mảng clients.
        if (FD_ISSET(listener, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listener, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd < 0) {
                perror("accept");
            } else {
                int added = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == 0) {
                        clients[i].fd = client_fd;
                        clients[i].used = 0;
                        clients[i].buffer[0] = '\0';
                        added = 1;
                        break;
                    }
                }

                if (!added) {
                    const char *full = "Server full\n";
                    send(client_fd, full, strlen(full), 0);
                    close(client_fd);
                } else {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                    printf("Client connected: %s:%d\n", ip, ntohs(client_addr.sin_port));
                    const char *help = "Welcome. Use: SUB <topic> or PUB <topic> <msg>\n";
                    send(client_fd, help, strlen(help), 0);
                }
            }
        }

        // Duyệt từng client để đọc command SUB/PUB.
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = clients[i].fd;
            if (fd <= 0 || !FD_ISSET(fd, &readfds)) {
                continue;
            }

            // Nhận dữ liệu từ client hiện tại.
            char tmp[BUF_SIZE];
            int n = recv(fd, tmp, sizeof(tmp), 0);
            if (n <= 0) {
                remove_client(clients, topics, i);
                continue;
            }

            // Ghép dữ liệu mới vào buffer đệm để xử lý theo từng dòng.
            if (clients[i].used + (size_t)n >= sizeof(clients[i].buffer)) {
                clients[i].used = 0;
                clients[i].buffer[0] = '\0';
            }

            memcpy(clients[i].buffer + clients[i].used, tmp, (size_t)n);
            clients[i].used += (size_t)n;
            clients[i].buffer[clients[i].used] = '\0';

            // Tách từng dòng lệnh hoàn chỉnh kết thúc bằng '\n'.
            char *line_start = clients[i].buffer;
            char *newline;
            while ((newline = strchr(line_start, '\n')) != NULL) {
                *newline = '\0';
                trim_newline(line_start);

                // SUB <topic>: thêm client vào danh sách nhận topic.
                if (strncmp(line_start, "SUB ", 4) == 0) {
                    char topic[MAX_TOPIC_LEN];
                    if (sscanf(line_start + 4, "%63s", topic) == 1) {
                        subscribe_client(topics, fd, topic);
                        char ok[128];
                        snprintf(ok, sizeof(ok), "Subscribed to %s\n", topic);
                        send(fd, ok, strlen(ok), 0);
                    } else {
                        const char *err = "Invalid SUB format. Use: SUB <topic>\n";
                        send(fd, err, strlen(err), 0);
                    }
                // UNSUB <topic>: xóa client khỏi danh sách nhận topic.
                } else if (strncmp(line_start, "UNSUB ", 6) == 0) {
                    char topic[MAX_TOPIC_LEN];
                    if (sscanf(line_start + 6, "%63s", topic) == 1) {
                        unsubscribe_client(topics, fd, topic);
                        char ok[128];
                        snprintf(ok, sizeof(ok), "Unsubscribed from %s\n", topic);
                        send(fd, ok, strlen(ok), 0);
                    } else {
                        const char *err = "Invalid UNSUB format. Use: UNSUB <topic>\n";
                        send(fd, err, strlen(err), 0);
                    }
                // PUB <topic> <msg>: phát msg đến tất cả subscriber của topic.
                } else if (strncmp(line_start, "PUB ", 4) == 0) {
                    char topic[MAX_TOPIC_LEN];
                    char msg[BUF_SIZE];
                    if (sscanf(line_start + 4, "%63s %1023[^\n]", topic, msg) == 2) {
                        send_to_subscribers(clients, topics, topic, msg, fd);
                    } else {
                        const char *err = "Invalid PUB format. Use: PUB <topic> <msg>\n";
                        send(fd, err, strlen(err), 0);
                    }
                } else if (line_start[0] != '\0') {
                    const char *err = "Unknown command. Use SUB, UNSUB or PUB.\n";
                    send(fd, err, strlen(err), 0);
                }

                // Chuyển sang dòng tiếp theo nếu client gửi nhiều lệnh cùng lúc.
                line_start = newline + 1;
            }

            // Giữ lại phần dữ liệu chưa đủ thành một dòng hoàn chỉnh.
            size_t remain = strlen(line_start);
            memmove(clients[i].buffer, line_start, remain + 1);
            clients[i].used = remain;
        }
    }

    close(listener);
    return 0;
}