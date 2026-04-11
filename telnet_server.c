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
#define BUF_SIZE 2048
#define USER_LEN 64
#define PASS_LEN 64
#define DB_FILE "users.txt"
#define OUT_FILE "out.txt"

typedef enum {
	AUTH_WAIT_USER = 0,
	AUTH_WAIT_PASS = 1,
	AUTH_DONE = 2
} AuthState;

typedef struct {
	int fd;
	AuthState state;
	char username[USER_LEN];
} Client;

static void trim_newline(char *s) {
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
		s[n - 1] = '\0';
		n--;
	}
}

static int authenticate_user(const char *username, const char *password) {
	FILE *f = fopen(DB_FILE, "r");
	if (f == NULL) {
		return 0;
	}

	char user[USER_LEN];
	char pass[PASS_LEN];
	while (fscanf(f, "%63s %63s", user, pass) == 2) {
		if (strcmp(user, username) == 0 && strcmp(pass, password) == 0) {
			fclose(f);
			return 1;
		}
	}

	fclose(f);
	return 0;
}

static void send_file_content(int fd, const char *path) {
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		const char *msg = "Cannot open output file.\n$ ";
		send(fd, msg, strlen(msg), 0);
		return;
	}

	char buffer[BUF_SIZE];
	size_t n;
	while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
		send(fd, buffer, n, 0);
	}
	fclose(f);

	const char *prompt = "\n$ ";
	send(fd, prompt, strlen(prompt), 0);
}

static void remove_client(Client clients[], int idx) {
	if (clients[idx].fd > 0) {
		close(clients[idx].fd);
	}
	clients[idx].fd = 0;
	clients[idx].state = AUTH_WAIT_USER;
	clients[idx].username[0] = '\0';
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
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons((uint16_t)port);

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

	Client clients[MAX_CLIENTS];
	memset(clients, 0, sizeof(clients));

	printf("Telnet server listening on port %d\n", port);
	printf("Credential file: %s\n", DB_FILE);

	while (1) {
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(listener, &readfds);
		int max_fd = listener;

		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (clients[i].fd > 0) {
				FD_SET(clients[i].fd, &readfds);
				if (clients[i].fd > max_fd) {
					max_fd = clients[i].fd;
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

		if (FD_ISSET(listener, &readfds)) {
			struct sockaddr_in caddr;
			socklen_t clen = sizeof(caddr);
			int cfd = accept(listener, (struct sockaddr *)&caddr, &clen);
			if (cfd < 0) {
				perror("accept");
			} else {
				int added = 0;
				for (int i = 0; i < MAX_CLIENTS; i++) {
					if (clients[i].fd == 0) {
						clients[i].fd = cfd;
						clients[i].state = AUTH_WAIT_USER;
						clients[i].username[0] = '\0';
						added = 1;
						break;
					}
				}

				if (!added) {
					const char *full = "Server full. Try again later.\n";
					send(cfd, full, strlen(full), 0);
					close(cfd);
				} else {
					printf("Client connected: %s:%d\n", inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
					const char *ask_user = "Username: ";
					send(cfd, ask_user, strlen(ask_user), 0);
				}
			}
		}

		for (int i = 0; i < MAX_CLIENTS; i++) {
			int fd = clients[i].fd;
			if (fd <= 0 || !FD_ISSET(fd, &readfds)) {
				continue;
			}

			char buf[BUF_SIZE];
			int n = recv(fd, buf, sizeof(buf) - 1, 0);
			if (n <= 0) {
				remove_client(clients, i);
				continue;
			}

			buf[n] = '\0';
			trim_newline(buf);

			if (clients[i].state == AUTH_WAIT_USER) {
				if (strlen(buf) == 0) {
					const char *ask_user = "Username: ";
					send(fd, ask_user, strlen(ask_user), 0);
					continue;
				}

				if (strlen(buf) >= sizeof(clients[i].username)) {
					const char *too_long =
						"Username too long (max 63 chars). Username: ";
					send(fd, too_long, strlen(too_long), 0);
					continue;
				}

				memcpy(clients[i].username, buf, strlen(buf) + 1);
				clients[i].state = AUTH_WAIT_PASS;
				const char *ask_pass = "Password: ";
				send(fd, ask_pass, strlen(ask_pass), 0);
				continue;
			}

			if (clients[i].state == AUTH_WAIT_PASS) {
				if (authenticate_user(clients[i].username, buf)) {
					clients[i].state = AUTH_DONE;
					const char *ok = "Login success. Enter command (type quit to exit).\n$ ";
					send(fd, ok, strlen(ok), 0);
				} else {
					const char *fail = "Login failed. Username: ";
					send(fd, fail, strlen(fail), 0);
					clients[i].state = AUTH_WAIT_USER;
					clients[i].username[0] = '\0';
				}
				continue;
			}

			if (strcmp(buf, "quit") == 0 || strcmp(buf, "exit") == 0) {
				const char *bye = "Bye.\n";
				send(fd, bye, strlen(bye), 0);
				remove_client(clients, i);
				continue;
			}

			if (strlen(buf) == 0) {
				const char *prompt = "$ ";
				send(fd, prompt, strlen(prompt), 0);
				continue;
			}

			char sys_cmd[BUF_SIZE + 64];
			snprintf(sys_cmd, sizeof(sys_cmd), "%s > %s 2>&1", buf, OUT_FILE);
			int rc = system(sys_cmd);
			if (rc == -1) {
				const char *msg = "Cannot execute command.\n$ ";
				send(fd, msg, strlen(msg), 0);
				continue;
			}

			send_file_content(fd, OUT_FILE);
		}
	}

	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (clients[i].fd > 0) {
			close(clients[i].fd);
		}
	}
	close(listener);
	return 0;
}
