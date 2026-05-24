#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>

#define CONFIG_PATH "/tmp/bsocket_config"
#define MAX_LINE_LEN 128

void read_config(char *socket_path, size_t size) {
    FILE *config = fopen(CONFIG_PATH, "r");
    if (!config) {
        perror("Failed to open config file");
        exit(EXIT_FAILURE);
    }
    if (!fgets(socket_path, size, config)) {
        fprintf(stderr, "Failed to read socket path\n");
        exit(EXIT_FAILURE);
    }
    socket_path[strcspn(socket_path, "\n")] = 0;
    fclose(config);
}

void interactive_mode(int sock_fd) {
    char line[MAX_LINE_LEN];
    char response[128];
    
    printf("Enter numbers (Ctrl+D to exit):\n");
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;
        
        if (write(sock_fd, line, strlen(line)) < 0) break;
        if (write(sock_fd, "\n", 1) < 0) break;
        
        int bytes = read(sock_fd, response, sizeof(response)-1);
        if (bytes <= 0) break;
        response[bytes] = '\0';
        printf("Response: %s", response);
    }
}

// Функция для чтения строки из сокета
int read_line(int sock_fd, char *buffer, size_t size) {
    size_t i = 0;
    char ch;
    while (i < size - 1) {
        ssize_t n = read(sock_fd, &ch, 1);
        if (n <= 0) return -1;
        if (ch == '\n') {
            buffer[i] = '\0';
            return i;
        }
        buffer[i++] = ch;
    }
    buffer[i] = '\0';
    return i;
}

void test_mode(int sock_fd, const char *filename, const char *logfile, int max_delay_ms) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open file");
        exit(1);
    }
    
    FILE *log = fopen(logfile, "w");
    if (!log) {
        perror("Failed to open log file");
        exit(1);
    }
    
    long long total_delay_us = 0;
    int numbers_processed = 0;
    struct timeval start, end;
    
    gettimeofday(&start, NULL);
    srand(time(NULL) ^ (getpid() << 16));
    
    char line[32];
    int line_pos = 0;
    int ch;
    
    // Побайтовое чтение с задержками
    while ((ch = fgetc(file)) != EOF) {
        // Случайная задержка с вероятностью 1/255
        if (max_delay_ms > 0 && (rand() % 255) == 0) {
            int delay_us = (rand() % (max_delay_ms * 1000)) + 1000;
            usleep(delay_us);
            total_delay_us += delay_us;
        }
        
        if (ch == '\n') {
            if (line_pos > 0) {
                line[line_pos] = '\0';
                
                // Отправка числа
                if (write(sock_fd, line, line_pos) < 0) break;
                if (write(sock_fd, "\n", 1) < 0) break;
                
                // Чтение ответа (читаем до \n)
                char response[64];
                if (read_line(sock_fd, response, sizeof(response)) < 0) break;
                numbers_processed++;
                line_pos = 0;
            }
        } else if (line_pos < 31) {
            line[line_pos++] = ch;
        }
    }
    
    // Проверка финального состояния (отправка 0)
    if (write(sock_fd, "0\n", 2) < 0) {
        perror("Failed to send final 0");
    }
    char final_response[64];
    if (read_line(sock_fd, final_response, sizeof(final_response)) < 0) {
        strcpy(final_response, "ERROR");
    }
    
    gettimeofday(&end, NULL);
    fclose(file);
    
    double elapsed = (end.tv_sec - start.tv_sec) + 
                     (end.tv_usec - start.tv_usec) / 1000000.0;
    
    fprintf(log, "Numbers processed: %d\n", numbers_processed);
    fprintf(log, "Total delay: %.6f\n", total_delay_us / 1000000.0);
    fprintf(log, "Elapsed time: %.6f\n", elapsed);
    fprintf(log, "Final server response: %s\n", final_response);
    fclose(log);
    
    printf("Test completed. Processed %d numbers, total delay %.3f sec, final response: %s\n", 
           numbers_processed, total_delay_us / 1000000.0, final_response);
}

int main(int argc, char *argv[]) {
    char socket_path[256];
    int opt;
    char *test_file = NULL;
    char *log_file = NULL;
    int delay_ms = 0;
    
    while ((opt = getopt(argc, argv, "f:d:l:")) != -1) {
        switch (opt) {
            case 'f': test_file = optarg; break;
            case 'd': delay_ms = atoi(optarg); break;
            case 'l': log_file = optarg; break;
            default:
                fprintf(stderr, "Usage: %s [-f file -d delay_ms -l logfile]\n", argv[0]);
                return 1;
        }
    }
    
    read_config(socket_path, sizeof(socket_path));
    
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);
    
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect failed");
        return 1;
    }
    
    if (test_file && log_file) {
        test_mode(sock_fd, test_file, log_file, delay_ms);
    } else if (!test_file && !log_file) {
        interactive_mode(sock_fd);
    } else {
        fprintf(stderr, "Both -f and -l are required for test mode\n");
        return 1;
    }
    
    close(sock_fd);
    return 0;
}