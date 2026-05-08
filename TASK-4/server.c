#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>
#include <limits.h>
#include <stdarg.h>

#define CONFIG_PATH "/tmp/bsocket_config"
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 1000
#define MAX_LINE_LEN 256

typedef struct {
    int fd;
    char buffer[BUFFER_SIZE];
    int buffer_len;
    bool active;
} client_t;

typedef struct {
    long long state;
    FILE *log_file;
    client_t clients[MAX_CLIENTS];
    int listen_fd;
} server_t;

server_t server;

void read_config(char *socket_path, size_t size) {
    FILE *config = fopen(CONFIG_PATH, "r");
    if (!config) {
        perror("Failed to open config file");
        exit(EXIT_FAILURE);
    }
    
    if (!fgets(socket_path, size, config)) {
        fprintf(stderr, "Failed to read socket path from config\n");
        fclose(config);
        exit(EXIT_FAILURE);
    }
    
    size_t len = strlen(socket_path);
    if (len > 0 && socket_path[len-1] == '\n') {
        socket_path[len-1] = '\0';
    }
    
    fclose(config);
}

void log_message(const char *format, ...) {
    if (!server.log_file) return;
    
    va_list args;
    va_start(args, format);
    vfprintf(server.log_file, format, args);
    va_end(args);
    fflush(server.log_file);
}

void log_state_info() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    void *heap_end = sbrk(0);
    log_message("[%ld.%06ld] State: %lld, Heap: %p\n", 
                (long)tv.tv_sec, (long)tv.tv_usec, server.state, heap_end);
}

void init_server(const char *socket_path) {
    memset(&server, 0, sizeof(server));
    server.state = 0;
    
    server.log_file = fopen("/tmp/brownian_bot.log", "w");
    if (!server.log_file) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
    
    server.listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server.listen_fd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    unlink(socket_path);
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    if (bind(server.listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server.listen_fd, MAX_CLIENTS) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    
    int flags = fcntl(server.listen_fd, F_GETFL, 0);
    fcntl(server.listen_fd, F_SETFL, flags | O_NONBLOCK);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        server.clients[i].fd = -1;
        server.clients[i].buffer_len = 0;
        server.clients[i].active = false;
    }
    
    log_message("Server initialized. Socket: %s\n", socket_path);
    log_state_info();
}

void add_client(int client_fd) {
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!server.clients[i].active) {
            server.clients[i].fd = client_fd;
            server.clients[i].buffer_len = 0;
            server.clients[i].active = true;
            log_message("Client connected on fd %d\n", client_fd);
            log_state_info();
            return;
        }
    }
    
    close(client_fd);
    log_message("No free slots for client\n");
}

void remove_client(int idx) {
    if (idx < 0 || idx >= MAX_CLIENTS || !server.clients[idx].active) return;
    
    log_message("Client disconnected on fd %d\n", server.clients[idx].fd);
    close(server.clients[idx].fd);
    server.clients[idx].active = false;
    server.clients[idx].fd = -1;
    server.clients[idx].buffer_len = 0;
}

void process_client_data(int idx) {
    client_t *client = &server.clients[idx];
    if (!client->active) return;
    
    char *line_start = client->buffer;
    int remaining = client->buffer_len;
    int processed = 0;
    
    while (remaining > 0) {
        char *newline = memchr(line_start, '\n', remaining);
        if (!newline) {
            // Неполная строка - сохраняем в начало буфера
            if (remaining > 0) {
                memmove(client->buffer, line_start, remaining);
                client->buffer_len = remaining;
            }
            break;
        }
        
        int line_len = newline - line_start;
        
        if (line_len > 0 && line_len < MAX_LINE_LEN) {
            char temp_buffer[MAX_LINE_LEN];
            memcpy(temp_buffer, line_start, line_len);
            temp_buffer[line_len] = '\0';
            
            char *endptr;
            long long num = strtoll(temp_buffer, &endptr, 10);
            
            if (*endptr == '\0') {
                long long old_state = server.state;
                server.state += num;
                
                log_message("Client %d, Received: %lld, Old: %lld, New: %lld\n", 
                           client->fd, num, old_state, server.state);
                
                char response[64];
                int resp_len = snprintf(response, sizeof(response), "%lld\n", server.state);
                
                ssize_t sent = write(client->fd, response, resp_len);
                if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    log_message("Error sending response to client %d\n", client->fd);
                }
            } else {
                log_message("Invalid number from client %d: %s\n", client->fd, temp_buffer);
                write(client->fd, "ERROR\n", 6);
            }
        }
        
        processed += line_len + 1;
        remaining -= (line_len + 1);
        line_start = newline + 1;
    }
    
    // Сдвигаем непрочитанные данные в начало буфера
    if (processed > 0 && processed < client->buffer_len) {
        memmove(client->buffer, client->buffer + processed, client->buffer_len - processed);
        client->buffer_len -= processed;
    } else if (processed >= client->buffer_len) {
        client->buffer_len = 0;
    }
}

void run_server() {
    fd_set read_fds;
    int max_fd;
    
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server.listen_fd, &read_fds);
        max_fd = server.listen_fd;
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (server.clients[i].active) {
                FD_SET(server.clients[i].fd, &read_fds);
                if (server.clients[i].fd > max_fd) {
                    max_fd = server.clients[i].fd;
                }
            }
        }
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        
        if (activity < 0) {
            if (errno == EINTR) continue;
            perror("select failed");
            break;
        }
        
        if (FD_ISSET(server.listen_fd, &read_fds)) {
            struct sockaddr_un client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_fd = accept(server.listen_fd, (struct sockaddr*)&client_addr, &addr_len);
            
            if (client_fd >= 0) {
                add_client(client_fd);
            }
        }
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (server.clients[i].active && FD_ISSET(server.clients[i].fd, &read_fds)) {
                char temp_buffer[BUFFER_SIZE];
                int bytes_read = read(server.clients[i].fd, temp_buffer, BUFFER_SIZE - 1);
                
                if (bytes_read <= 0) {
                    remove_client(i);
                } else {
                    client_t *client = &server.clients[i];
                    if (client->buffer_len + bytes_read < BUFFER_SIZE) {
                        memcpy(client->buffer + client->buffer_len, temp_buffer, bytes_read);
                        client->buffer_len += bytes_read;
                        process_client_data(i);
                    } else {
                        log_message("Buffer full for client fd %d, disconnecting\n", client->fd);
                        remove_client(i);
                    }
                }
            }
        }
    }
}

int main() {
    char socket_path[256];
    
    signal(SIGPIPE, SIG_IGN);
    
    read_config(socket_path, sizeof(socket_path));
    init_server(socket_path);
    run_server();
    
    fclose(server.log_file);
    close(server.listen_fd);
    unlink(socket_path);
    
    return 0;
}