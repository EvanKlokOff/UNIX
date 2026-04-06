#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>


// Глобальная переменная для управления бесконечным циклом
volatile sig_atomic_t keep_running = 1;

// Обработчик сигнала SIGINT
void sigint_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

int setup_signal_handler(void) {
    struct sigaction sa = {0};  // Обнуляем всё
    
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    
    return 0;
}


void print_usage(const char *prog_name) {
    fprintf(stderr, "Использование: %s -f <файл_для_блокировки> -s <файл_статистики>\n", prog_name);
}

int main(int argc, char *argv[]) {
    
    srand(time(NULL) ^ getpid());

    int attempt = 0;
    int opt;
    char *target_file = NULL;
    char *stats_file = NULL; 

    while ((opt = getopt(argc, argv, "f:s:")) != -1) {
        switch (opt) {
            case 'f':
                target_file = optarg;
                break;
            case 's':
                stats_file = optarg;
                break;
            default:    
                print_usage(argv[0]);
                return EXIT_SUCCESS;
        }
    }

    
    if (!target_file || !stats_file) {
        fprintf(stderr, "Критическая ошибка: необходимо указать параметры -f и -s\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }


    if(setup_signal_handler() != 0){
        return EXIT_FAILURE;
    }

    char lock_file[1024];
    int needed = snprintf(lock_file, sizeof(lock_file), "%s.lck", target_file);
    
    if (needed >= sizeof(lock_file)) {
        fprintf(stderr, "Ошибка: имя файла блокировки слишком длинное\n");
        return EXIT_FAILURE;
    }
    
    int successful_locks = 0;
    pid_t my_pid = getpid();

    while (keep_running) {
        // Попытка атомарно создать файл блокировки
        int fd;
        do {
            fd = open(lock_file, O_WRONLY | O_CREAT | O_EXCL, 0644);
        } while (fd < 0 && errno == EINTR && keep_running);
        
        if (fd < 0) {
            if (errno == EEXIST) {
                // Экспоненциальное ожидание файла с максимумом для повышения равномерности распределения блокировок
                int delay = 50000 * (1 << (attempt > 5 ? 5 : attempt));
                delay += rand() % 50000;  // Добавляем случайность
                if (delay > 500000) delay = 500000;
                usleep(delay);
                attempt++;
                continue;
            } else {
                perror("Критическая ошибка: не удалось открыть/создать файл блокировки");
                return EXIT_FAILURE;
            }
        }
        attempt = 0; 
        // Блокировка получена, записываем свой PID
        char pid_buf[32];
        int len = snprintf(pid_buf, sizeof(pid_buf), "%d\n", my_pid);

        if (write(fd, pid_buf, len) != len) {
            perror("Критическая ошибка: ошибка записи PID в файл блокировки");
            close(fd);
            unlink(lock_file);
            return EXIT_FAILURE;
        }
        close(fd);

        // --- КРИТИЧЕСКАЯ СЕКЦИЯ ---
        // Имитация работы с общим файлом (сон для наглядности)
        for (int i = 0; i < 100 && keep_running; i++) {
            usleep(10000);  // 10ms * 100 = 1 секунда
        }

        if (!keep_running) {
            unlink(lock_file);
            break;
        }

        // Проверка: существует ли еще файл и наш ли там PID
        do {
            fd = open(lock_file, O_RDONLY);
        } while (fd < 0 && errno == EINTR && keep_running);
        
        if (fd < 0) {
            fprintf(stderr, "Критическая ошибка [PID %d]: Файл блокировки исчез во время работы!\n", my_pid);
            return EXIT_FAILURE;
        }

        char read_buf[32] = {0};
        ssize_t bytes_read = read(fd, read_buf, sizeof(read_buf) - 1);
        close(fd);

        if (bytes_read <= 0) {
            fprintf(stderr, "Критическая ошибка [PID %d]: Не удалось прочитать PID из файла блокировки!\n", my_pid);
            return EXIT_FAILURE;
        }

        if (atoi(read_buf) != my_pid) {
            fprintf(stderr, "Критическая ошибка [PID %d]: Блокировка перехвачена процессом %d!\n", my_pid, atoi(read_buf));
            return EXIT_FAILURE;
        }

        // Если все проверки пройдены, снимаем блокировку
        if (unlink(lock_file) != 0) {
            perror("Критическая ошибка: не удалось удалить файл блокировки");
            return EXIT_FAILURE;
        }

        successful_locks++;

        // ожидание перед следующей попыткой, чтобы избежать голодания других процессов и для повышения равномерности распределения блокировок
        usleep(20000 + (rand() % 130000));
    }

    int stats_fd = -1;
    
    do {
        stats_fd = open(stats_file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    } while (stats_fd < 0 && errno == EINTR && keep_running);

    if (stats_fd < 0) {
        perror("Критическая ошибка: не удалось открыть файл статистики");
        return EXIT_FAILURE;
    }

    char stat_buf[64];
    int stat_len = snprintf(stat_buf, sizeof(stat_buf), "%d\n", successful_locks);
        
    if (write(stats_fd, stat_buf, stat_len) != stat_len) {
        perror("Ошибка записи статистики");
    }
 
    close(stats_fd);
    return 0;
}