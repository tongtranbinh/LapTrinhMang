#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define PORT 8080
#define THREAD_COUNT 4
#define BUF_SIZE 16384

// TODO: Điền thông tin của bạn ở đây
#define MY_NAME "Tong Tran Binh"
#define MY_MSSV "20235275"

// TODO: Điền thông tin bạn của bạn ở đây
#define FRIEND_NAME "Tong Tran Binh"
#define FRIEND_MSSV "20235275"

static int g_listener = -1;
static pthread_mutex_t g_accept_lock = PTHREAD_MUTEX_INITIALIZER;

static int send_all(int fd, const void *data, size_t length) {
    const char *cursor = (const char *)data;
    while (length > 0) {
        ssize_t sent = send(fd, cursor, length, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        cursor += (size_t)sent;
        length -= (size_t)sent;
    }
    return 0;
}

static int append_text(char *buffer, size_t capacity, size_t *used, const char *text) {
    size_t text_len = strlen(text);
    if (*used + text_len >= capacity) {
        return -1;
    }
    memcpy(buffer + *used, text, text_len);
    *used += text_len;
    buffer[*used] = '\0';
    return 0;
}

static int append_format(char *buffer, size_t capacity, size_t *used, const char *format, ...) {
    if (*used >= capacity) {
        return -1;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + *used, capacity - *used, format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= capacity - *used) {
        return -1;
    }

    *used += (size_t)written;
    return 0;
}

static int append_html_escaped(char *buffer, size_t capacity, size_t *used, const char *text) {
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; cursor++) {
        const char *replacement = NULL;
        switch (*cursor) {
            case '&': replacement = "&amp;"; break;
            case '<': replacement = "&lt;"; break;
            case '>': replacement = "&gt;"; break;
            case '"': replacement = "&quot;"; break;
            case '\'': replacement = "&#39;"; break;
            default: break;
        }

        if (replacement) {
            if (append_text(buffer, capacity, used, replacement) != 0) {
                return -1;
            }
        } else {
            if (*used + 1 >= capacity) {
                return -1;
            }
            buffer[(*used)++] = (char)*cursor;
            buffer[*used] = '\0';
        }
    }
    return 0;
}

static int append_url_encoded(char *buffer, size_t capacity, size_t *used, const char *text) {
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; cursor++) {
        unsigned char c = *cursor;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            if (*used + 1 >= capacity) {
                return -1;
            }
            buffer[(*used)++] = (char)c;
            buffer[*used] = '\0';
        } else if (c == ' ') {
            if (*used + 1 >= capacity) {
                return -1;
            }
            buffer[(*used)++] = '+';
            buffer[*used] = '\0';
        } else {
            if (*used + 3 >= capacity) {
                return -1;
            }
            buffer[(*used)++] = '%';
            buffer[(*used)++] = hex[(c >> 4) & 0x0F];
            buffer[(*used)++] = hex[c & 0x0F];
            buffer[*used] = '\0';
        }
    }
    return 0;
}

static void url_decode_inplace(char *text) {
    char *read = text;
    char *write = text;
    while (*read != '\0') {
        if (*read == '+') {
            *write++ = ' ';
            read++;
        } else if (*read == '%' && isxdigit((unsigned char)read[1]) && isxdigit((unsigned char)read[2])) {
            char hex_value[3] = { read[1], read[2], '\0' };
            *write++ = (char)strtol(hex_value, NULL, 16);
            read += 3;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static int parse_http_request_line(const char *req, char *method_out, size_t method_cap,
                                   char *target_out, size_t target_cap) {
    const char *line_end = strstr(req, "\r\n");
    if (!line_end) {
        return -1;
    }

    const char *sp1 = memchr(req, ' ', (size_t)(line_end - req));
    if (!sp1) {
        return -1;
    }

    const char *sp2 = memchr(sp1 + 1, ' ', (size_t)(line_end - (sp1 + 1)));
    if (!sp2) {
        return -1;
    }

    size_t method_len = (size_t)(sp1 - req);
    size_t target_len = (size_t)(sp2 - (sp1 + 1));
    if (method_len == 0 || target_len == 0 || method_len >= method_cap || target_len >= target_cap) {
        return -1;
    }

    memcpy(method_out, req, method_len);
    method_out[method_len] = '\0';
    memcpy(target_out, sp1 + 1, target_len);
    target_out[target_len] = '\0';
    return 0;
}

static long parse_content_length(const char *req) {
    const char *needle = "Content-Length:";
    const char *match = strstr(req, needle);
    if (!match) {
        return 0;
    }

    match += strlen(needle);
    while (*match == ' ' || *match == '\t') {
        match++;
    }

    return strtol(match, NULL, 10);
}

static ssize_t read_http_request(int client_fd, char *buffer, size_t capacity) {
    size_t total = 0;
    buffer[0] = '\0';

    while (total + 1 < capacity) {
        ssize_t received = recv(client_fd, buffer + total, capacity - 1 - total, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (received == 0) {
            break;
        }

        total += (size_t)received;
        buffer[total] = '\0';

        char *header_end = strstr(buffer, "\r\n\r\n");
        if (header_end) {
            size_t header_bytes = (size_t)(header_end - buffer) + 4;
            long content_length = parse_content_length(buffer);
            if (content_length < 0) {
                content_length = 0;
            }

            if (total >= header_bytes + (size_t)content_length) {
                break;
            }
        }
    }

    return (ssize_t)total;
}

static void send_http_response(int client_fd, int status_code, const char *reason,
                               const char *content_type, const char *body) {
    char header[BUF_SIZE];
    long body_len = (long)strlen(body);

    int header_len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code,
        reason,
        content_type,
        body_len);

    if (header_len > 0) {
        (void)send_all(client_fd, header, (size_t)header_len);
    }
    (void)send_all(client_fd, body, (size_t)body_len);
}

static void send_http_file(int client_fd, const char *content_type, FILE *file, long file_size) {
    char header[BUF_SIZE];
    int header_len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        content_type,
        file_size);

    if (header_len > 0) {
        if (send_all(client_fd, header, (size_t)header_len) != 0) {
            return;
        }
    }

    char file_buffer[BUF_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0) {
        if (send_all(client_fd, file_buffer, bytes_read) != 0) {
            return;
        }
    }
}

static const char *mime_type_from_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return "application/octet-stream";
    }

    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".c") == 0 ||
        strcasecmp(dot, ".h") == 0 || strcasecmp(dot, ".md") == 0 ||
        strcasecmp(dot, ".log") == 0 || strcasecmp(dot, ".csv") == 0 ||
        strcasecmp(dot, ".json") == 0 || strcasecmp(dot, ".xml") == 0 ||
        strcasecmp(dot, ".css") == 0 || strcasecmp(dot, ".js") == 0) {
        return "text/plain; charset=utf-8";
    }
    if (strcasecmp(dot, ".png") == 0) {
        return "image/png";
    }
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcasecmp(dot, ".gif") == 0) {
        return "image/gif";
    }
    if (strcasecmp(dot, ".bmp") == 0) {
        return "image/bmp";
    }
    if (strcasecmp(dot, ".webp") == 0) {
        return "image/webp";
    }
    if (strcasecmp(dot, ".mp3") == 0) {
        return "audio/mpeg";
    }
    if (strcasecmp(dot, ".wav") == 0) {
        return "audio/wav";
    }
    if (strcasecmp(dot, ".ogg") == 0) {
        return "audio/ogg";
    }
    if (strcasecmp(dot, ".m4a") == 0) {
        return "audio/mp4";
    }
    if (strcasecmp(dot, ".mp4") == 0) {
        return "video/mp4";
    }
    if (strcasecmp(dot, ".webm") == 0) {
        return "video/webm";
    }
    if (strcasecmp(dot, ".mkv") == 0) {
        return "video/x-matroska";
    }
    if (strcasecmp(dot, ".avi") == 0) {
        return "video/x-msvideo";
    }

    return "application/octet-stream";
}

static int contains_parent_segment(const char *path) {
    const char *cursor = path;
    while (*cursor != '\0') {
        while (*cursor == '/') {
            cursor++;
        }

        const char *segment_start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }

        size_t segment_len = (size_t)(cursor - segment_start);
        if (segment_len == 2 && segment_start[0] == '.' && segment_start[1] == '.') {
            return 1;
        }
    }
    return 0;
}

static void join_url_path(char *out, size_t out_cap, const char *base, const char *name, int is_dir) {
    if (strcmp(base, "/") == 0) {
        snprintf(out, out_cap, "/%s%s", name, is_dir ? "/" : "");
        return;
    }

    size_t len = strlen(base);
    if (len > 0 && base[len - 1] == '/') {
        snprintf(out, out_cap, "%s%s%s", base, name, is_dir ? "/" : "");
    } else {
        snprintf(out, out_cap, "%s/%s%s", base, name, is_dir ? "/" : "");
    }
}

static void render_calc_page(char *body, size_t body_cap, const char *method,
                             const char *expression, const char *result, const char *error) {
    size_t used = 0;
    body[0] = '\0';

    append_text(body, body_cap, &used,
                "<html><head><meta charset=\"utf-8\"><title>HTTP Calculator</title>"
                "<style>"
                "body{font-family:Arial,sans-serif;margin:0;padding:0;background:linear-gradient(135deg,#f7fbff,#eef4ff);color:#10233f}"
                ".wrap{max-width:900px;margin:0 auto;padding:32px}"
                ".card{background:#fff;border-radius:18px;box-shadow:0 18px 50px rgba(16,35,63,.12);padding:24px;margin-bottom:20px}"
                "h1,h2{margin-top:0}"
                "label{display:block;margin:12px 0 6px;font-weight:700}"
                "input,select,button{font:inherit;padding:12px 14px;border:1px solid #c9d7ee;border-radius:12px;width:100%;box-sizing:border-box}"
                "button{background:#0f62fe;color:#fff;border:none;cursor:pointer;font-weight:700}"
                ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px}"
                ".result{font-size:1.2rem;background:#f1f6ff;padding:14px 16px;border-radius:12px}"
                ".error{background:#fff1f1;color:#8b1d1d;padding:14px 16px;border-radius:12px}"
                ".nav a{margin-right:12px}"
                "</style></head><body><div class=\"wrap\">"
                "<div class=\"card nav\"><a href=\"/\">File browser</a><a href=\"/calc\">Calculator</a></div>"
                "<div class=\"card\"><h1>HTTP Calculator</h1><p>Send parameters by GET or POST with fields <b>op</b>, <b>a</b>, and <b>b</b>.</p>");

    if (expression && result) {
        append_text(body, body_cap, &used, "<div class=\"result\"><b>");
        append_html_escaped(body, body_cap, &used, expression);
        append_text(body, body_cap, &used, " = ");
        append_html_escaped(body, body_cap, &used, result);
        append_text(body, body_cap, &used, "</b></div>");
    }

    if (error && error[0] != '\0') {
        append_text(body, body_cap, &used, "<div class=\"error\">");
        append_html_escaped(body, body_cap, &used, error);
        append_text(body, body_cap, &used, "</div>");
    }

    append_text(body, body_cap, &used,
                "<form method=\"GET\" action=\"/calc\"><h2>GET</h2><div class=\"grid\">"
                "<div><label>Operator</label><select name=\"op\">"
                "<option value=\"cong\">Cộng (+)</option>"
                "<option value=\"tru\">Trừ (-)</option>"
                "<option value=\"nhan\">Nhân (*)</option>"
                "<option value=\"chia\">Chia (/)</option>"
                "</select></div>"
                "<div><label>Operand A</label><input name=\"a\" type=\"number\" step=\"any\" value=\"2\"></div>"
                "<div><label>Operand B</label><input name=\"b\" type=\"number\" step=\"any\" value=\"3\"></div>"
                "</div><p><button type=\"submit\">Calculate by GET</button></p></form>");

    append_text(body, body_cap, &used,
                "<form method=\"POST\" action=\"/calc\"><h2>POST</h2><div class=\"grid\">"
                "<div><label>Operator</label><select name=\"op\">"
                "<option value=\"cong\">Cộng (+)</option>"
                "<option value=\"tru\">Trừ (-)</option>"
                "<option value=\"nhan\">Nhân (*)</option>"
                "<option value=\"chia\">Chia (/)</option>"
                "</select></div>"
                "<div><label>Operand A</label><input name=\"a\" type=\"number\" step=\"any\" value=\"2\"></div>"
                "<div><label>Operand B</label><input name=\"b\" type=\"number\" step=\"any\" value=\"3\"></div>"
                "</div><p><button type=\"submit\">Calculate by POST</button></p></form>");

    append_format(body, body_cap, &used,
                  "<p><small>Request method: %s</small></p></div></div></body></html>",
                  method ? method : "GET");
}

static int read_param_value(const char *params, const char *key, char *out, size_t out_cap) {
    size_t key_len = strlen(key);
    const char *cursor = params;

    while (cursor && *cursor != '\0') {
        const char *segment_end = strchr(cursor, '&');
        size_t segment_len = segment_end ? (size_t)(segment_end - cursor) : strlen(cursor);
        const char *equal = memchr(cursor, '=', segment_len);
        if (equal) {
            size_t current_key_len = (size_t)(equal - cursor);
            if (current_key_len == key_len && strncmp(cursor, key, key_len) == 0) {
                size_t value_len = segment_len - current_key_len - 1;
                if (value_len >= out_cap) {
                    return -1;
                }
                memcpy(out, equal + 1, value_len);
                out[value_len] = '\0';
                url_decode_inplace(out);
                return 0;
            }
        }

        if (!segment_end) {
            break;
        }
        cursor = segment_end + 1;
    }

    return -1;
}

static int calculate_expression_from_params(const char *params, char *expression_out,
                                            size_t expression_cap, char *result_out,
                                            size_t result_cap, char *error_out,
                                            size_t error_cap) {
    char op[64];
    char a_text[64];
    char b_text[64];
    double a = 0.0;
    double b = 0.0;

    error_out[0] = '\0';
    if (read_param_value(params, "op", op, sizeof(op)) != 0 ||
        read_param_value(params, "a", a_text, sizeof(a_text)) != 0 ||
        read_param_value(params, "b", b_text, sizeof(b_text)) != 0) {
        snprintf(error_out, error_cap, "Missing parameters. Please provide op, a, and b.");
        return -1;
    }

    char *end_a = NULL;
    char *end_b = NULL;
    a = strtod(a_text, &end_a);
    b = strtod(b_text, &end_b);
    if (end_a == a_text || *end_a != '\0' || end_b == b_text || *end_b != '\0') {
        snprintf(error_out, error_cap, "Invalid operand(s). Please enter numeric values.");
        return -1;
    }

    double result = 0.0;
    const char *symbol = NULL;
    if (strcmp(op, "cong") == 0 || strcmp(op, "add") == 0 || strcmp(op, "+") == 0) {
        result = a + b;
        symbol = "+";
    } else if (strcmp(op, "tru") == 0 || strcmp(op, "sub") == 0 || strcmp(op, "-") == 0) {
        result = a - b;
        symbol = "-";
    } else if (strcmp(op, "nhan") == 0 || strcmp(op, "mul") == 0 || strcmp(op, "*") == 0) {
        result = a * b;
        symbol = "*";
    } else if (strcmp(op, "chia") == 0 || strcmp(op, "div") == 0 || strcmp(op, "/") == 0) {
        if (b == 0.0) {
            snprintf(error_out, error_cap, "Cannot divide by zero.");
            return -1;
        }
        result = a / b;
        symbol = "/";
    } else {
        snprintf(error_out, error_cap, "Unsupported operator. Use cong, tru, nhan, or chia.");
        return -1;
    }

    snprintf(expression_out, expression_cap, "%.10g %s %.10g", a, symbol, b);
    snprintf(result_out, result_cap, "%.10g", result);
    return 0;
}

static void handle_calculator_request(int client_fd, const char *method, const char *query,
                                      const char *body) {
    char expression[128];
    char result[128];
    char error[256];
    char page[BUF_SIZE];

    if (strcmp(method, "GET") == 0 && query && query[0] != '\0') {
        if (calculate_expression_from_params(query, expression, sizeof(expression), result,
                                             sizeof(result), error, sizeof(error)) == 0) {
            render_calc_page(page, sizeof(page), method, expression, result, NULL);
        } else {
            render_calc_page(page, sizeof(page), method, NULL, NULL, error);
        }
    } else if (strcmp(method, "POST") == 0 && body && body[0] != '\0') {
        if (calculate_expression_from_params(body, expression, sizeof(expression), result,
                                             sizeof(result), error, sizeof(error)) == 0) {
            render_calc_page(page, sizeof(page), method, expression, result, NULL);
        } else {
            render_calc_page(page, sizeof(page), method, NULL, NULL, error);
        }
    } else {
        render_calc_page(page, sizeof(page), method, NULL, NULL, NULL);
    }

    send_http_response(client_fd, 200, "OK", "text/html; charset=utf-8", page);
}

static void handle_directory_listing(int client_fd, const char *request_path, const char *fs_path) {
    DIR *dir = opendir(fs_path);
    if (!dir) {
        const char *body = "Directory not found or not accessible";
        send_http_response(client_fd, 404, "Not Found", "text/plain; charset=utf-8", body);
        return;
    }

    char body[BUF_SIZE];
    size_t used = 0;
    body[0] = '\0';

    append_text(body, sizeof(body), &used,
                "<html><head><meta charset=\"utf-8\"><title>Directory listing</title>"
                "<style>"
                "body{font-family:Arial,sans-serif;margin:0;padding:0;background:linear-gradient(135deg,#fffaf3,#f3f7ff);color:#14213d}"
                ".wrap{max-width:980px;margin:0 auto;padding:32px}"
                ".card{background:#fff;border-radius:18px;box-shadow:0 18px 50px rgba(20,33,61,.12);padding:24px}"
                "h1{margin-top:0}"
                "ul{list-style:none;padding:0;margin:0}"
                "li{padding:10px 0;border-bottom:1px solid #edf0f5}"
                "a{text-decoration:none;color:#0f62fe}"
                "strong a{color:#0c4a8a}"
                "em a{color:#3b4252}"
                ".nav a{margin-right:12px}"
                "</style></head><body><div class=\"wrap\">"
                "<div class=\"card nav\"><a href=\"/\">Root</a><a href=\"/calc\">Calculator</a></div>"
                "<div class=\"card\"><h1>Index of ");
    append_html_escaped(body, sizeof(body), &used, request_path);
    append_text(body, sizeof(body), &used, "</h1><ul>");

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child_fs_path[PATH_MAX];
        char child_url_path[PATH_MAX];
        snprintf(child_fs_path, sizeof(child_fs_path), "%s/%s", fs_path, entry->d_name);

        struct stat st;
        if (stat(child_fs_path, &st) != 0) {
            continue;
        }

        int is_dir = S_ISDIR(st.st_mode);
        join_url_path(child_url_path, sizeof(child_url_path), request_path, entry->d_name, is_dir);

        append_text(body, sizeof(body), &used, "<li>");
        if (is_dir) {
            append_text(body, sizeof(body), &used, "<strong><a href=\"");
        } else {
            append_text(body, sizeof(body), &used, "<em><a href=\"");
        }

        append_url_encoded(body, sizeof(body), &used, child_url_path);
        append_text(body, sizeof(body), &used, "\">");
        append_html_escaped(body, sizeof(body), &used, entry->d_name);
        if (is_dir) {
            append_text(body, sizeof(body), &used, "/</a></strong>");
        } else {
            append_text(body, sizeof(body), &used, "</a></em>");
        }
        append_text(body, sizeof(body), &used, "</li>");
    }

    append_text(body, sizeof(body), &used, "</ul></div></div></body></html>");
    closedir(dir);

    send_http_response(client_fd, 200, "OK", "text/html; charset=utf-8", body);
}

static void handle_file_response(int client_fd, const char *fs_path) {
    struct stat st;
    if (stat(fs_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        const char *body = "File not found";
        send_http_response(client_fd, 404, "Not Found", "text/plain; charset=utf-8", body);
        return;
    }

    FILE *file = fopen(fs_path, "rb");
    if (!file) {
        const char *body = "Unable to open file";
        send_http_response(client_fd, 403, "Forbidden", "text/plain; charset=utf-8", body);
        return;
    }

    send_http_file(client_fd, mime_type_from_path(fs_path), file, (long)st.st_size);
    fclose(file);
}

static void handle_client(int client_fd) {
    char buf[BUF_SIZE];

    ssize_t ret = read_http_request(client_fd, buf, sizeof(buf));
    if (ret <= 0) {
        close(client_fd);
        return;
    }

    printf("Request from client:\n%s\n", buf);

    char method[16];
    char target[1024];
    if (parse_http_request_line(buf, method, sizeof(method), target, sizeof(target)) != 0) {
        const char *body = "Bad Request";
        send_http_response(client_fd, 400, "Bad Request", "text/plain; charset=utf-8", body);
        close(client_fd);
        return;
    }

    char *query = strchr(target, '?');
    if (query) {
        *query++ = '\0';
    }

    const char *body_start = strstr(buf, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
    } else {
        body_start = "";
    }

    if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0) {
        const char *body = "Method Not Allowed";
        send_http_response(client_fd, 405, "Method Not Allowed", "text/plain; charset=utf-8",
                           body);
        close(client_fd);
        return;
    }

    if (strcmp(target, "/calc") == 0 || strcmp(target, "/calc/") == 0) {
        handle_calculator_request(client_fd, method, query ? query : "", body_start);
    } else if (strcmp(target, "/") == 0) {
        handle_directory_listing(client_fd, "/", ".");
    } else if (strcmp(target, "/me") == 0) {
        char body[BUF_SIZE];
        size_t used = 0;
        body[0] = '\0';
        append_text(body, sizeof(body), &used,
                    "<html><head><meta charset=\"utf-8\"><title>/me</title></head>"
                    "<body><h1>Gioi thieu ve toi</h1>"
                    "<p><b>Ho ten:</b> ");
        append_html_escaped(body, sizeof(body), &used, MY_NAME);
        append_text(body, sizeof(body), &used, "</p><p><b>MSSV:</b> ");
        append_html_escaped(body, sizeof(body), &used, MY_MSSV);
        append_text(body, sizeof(body), &used, "</p></body></html>");
        send_http_response(client_fd, 200, "OK", "text/html; charset=utf-8", body);
    } else if (strcmp(target, "/friend") == 0) {
        char body[BUF_SIZE];
        size_t used = 0;
        body[0] = '\0';
        append_text(body, sizeof(body), &used,
                    "<html><head><meta charset=\"utf-8\"><title>/friend</title></head>"
                    "<body><h1>Gioi thieu ve ban cua toi</h1>"
                    "<p><b>Ho ten:</b> ");
        append_html_escaped(body, sizeof(body), &used, FRIEND_NAME);
        append_text(body, sizeof(body), &used, "</p><p><b>MSSV:</b> ");
        append_html_escaped(body, sizeof(body), &used, FRIEND_MSSV);
        append_text(body, sizeof(body), &used, "</p></body></html>");
        send_http_response(client_fd, 200, "OK", "text/html; charset=utf-8", body);
    } else {
        char decoded_path[PATH_MAX];
        char path_buffer[PATH_MAX + 1];

        strncpy(decoded_path, target, sizeof(decoded_path) - 1);
        decoded_path[sizeof(decoded_path) - 1] = '\0';
        url_decode_inplace(decoded_path);

        if (contains_parent_segment(decoded_path)) {
            const char *body = "Forbidden";
            send_http_response(client_fd, 403, "Forbidden", "text/plain; charset=utf-8", body);
        } else {
            const char *relative = decoded_path;
            if (relative[0] == '/') {
                relative++;
            }

            if (relative[0] == '\0') {
                handle_directory_listing(client_fd, "/", ".");
            } else {
                snprintf(path_buffer, sizeof(path_buffer), ".%s", decoded_path);

                struct stat st;
                if (stat(path_buffer, &st) != 0) {
                    const char *body = "Not Found";
                    send_http_response(client_fd, 404, "Not Found", "text/plain; charset=utf-8", body);
                } else if (S_ISDIR(st.st_mode)) {
                    handle_directory_listing(client_fd, decoded_path, path_buffer);
                } else {
                    handle_file_response(client_fd, path_buffer);
                }
            }
        }
    }

    close(client_fd);
}

static void *worker_loop(void *arg) {
    long worker_id = (long)arg;
    pthread_detach(pthread_self());

    printf("Worker %ld started\n", worker_id);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        pthread_mutex_lock(&g_accept_lock);
        int client_fd = accept(g_listener, (struct sockaddr *)&client_addr, &client_len);
        pthread_mutex_unlock(&g_accept_lock);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        handle_client(client_fd);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    g_listener = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listener < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(g_listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_listener, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(g_listener);
        exit(1);
    }

    if (listen(g_listener, 64) < 0) {
        perror("listen");
        close(g_listener);
        exit(1);
    }

    printf("HTTP server running on port %d...\n", PORT);

    for (long i = 0; i < THREAD_COUNT; i++) {
        pthread_t th;
        if (pthread_create(&th, NULL, worker_loop, (void *)(i + 1)) != 0) {
            perror("pthread_create");
            close(g_listener);
            exit(1);
        }
    }

    while (1) {
        pause();
    }
}