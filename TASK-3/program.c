#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include <stdarg.h>
#include <errno.h>

#define MAX_CMD_LEN 1024
#define MAX_PROC 64
#define CONFIG_LINE_MAX 2048
#define PID_FILE "/tmp/myinit.pid"

typedef struct {
    char *cmd;
    char **argv;
    int argc;
    char *stdin_file;
    char *stdout_file;
    pid_t pid;
    int active;
    int restart_disabled;  // Запрет на рестарт (при перезагрузке конфига)
} proc_entry_t;

proc_entry_t processes[MAX_PROC];
int num_procs = 0;
int running = 1;
int reloading = 0;
const char *log_file_path = "/tmp/myinit.log";
const char *config_file_path = NULL;

void log_message(const char *msg) {
    FILE *log = fopen(log_file_path, "a");
    if (log) {
        time_t now = time(NULL);
        char *time_str = ctime(&now);
        time_str[strlen(time_str) - 1] = '\0';
        fprintf(log, "[%s] %s\n", time_str, msg);
        fflush(log);
        fclose(log);
    }
}

void log_formatted(const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log_message(buffer);
}

void create_pid_file() {
    FILE *pid_file = fopen(PID_FILE, "w");
    if (pid_file) {
        fprintf(pid_file, "%d\n", getpid());
        fflush(pid_file);
        fclose(pid_file);
        chmod(PID_FILE, 0644);
    } else {
        log_formatted("Failed to create PID file %s: %s", PID_FILE, strerror(errno));
    }
}

void remove_pid_file() {
    unlink(PID_FILE);
}

void free_process(proc_entry_t *proc) {
    if (proc->cmd) free(proc->cmd);
    if (proc->argv) {
        for (int i = 0; i < proc->argc; i++) {
            if (proc->argv[i]) free(proc->argv[i]);
        }
        free(proc->argv);
    }
    if (proc->stdin_file) free(proc->stdin_file);
    if (proc->stdout_file) free(proc->stdout_file);
    memset(proc, 0, sizeof(proc_entry_t));
    proc->pid = -1;
}

void cleanup_all_processes() {
    log_message("Cleaning up all processes");
    
    // Сначала отключаем авторестарт для всех процессов
    for (int i = 0; i < num_procs; i++) {
        processes[i].restart_disabled = 1;
        if (processes[i].active) {
            kill(processes[i].pid, SIGTERM);
            log_formatted("Sent SIGTERM to process %d: %s (pid %d)", i, processes[i].cmd, processes[i].pid);
        }
    }
    
    // Ждем завершения процессов
    int timeout = 5;
    while (timeout-- > 0) {
        int all_dead = 1;
        for (int i = 0; i < num_procs; i++) {
            if (processes[i].active) {
                pid_t result = waitpid(processes[i].pid, NULL, WNOHANG);
                if (result == processes[i].pid) {
                    processes[i].active = 0;
                } else if (result == 0) {
                    all_dead = 0;
                }
            }
        }
        if (all_dead) break;
        sleep(1);
    }
    
    // Принудительно убиваем оставшиеся
    for (int i = 0; i < num_procs; i++) {
        if (processes[i].active) {
            kill(processes[i].pid, SIGKILL);
            waitpid(processes[i].pid, NULL, 0);
            processes[i].active = 0;
            log_formatted("Force killed process %d: %s", i, processes[i].cmd);
        }
    }
}

void start_process(int idx) {
    if (idx >= num_procs) {
        log_formatted("Invalid process index: %d", idx);
        return;
    }
    if (processes[idx].active) {
        return;
    }
    if (!processes[idx].cmd) {
        log_formatted("Process %d has no command", idx);
        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        log_formatted("Fork failed for process %d: %s (%s)", idx, processes[idx].cmd, strerror(errno));
        return;
    }
    
    if (pid == 0) {
        // Дочерний процесс
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        
        // Перенаправление stdin
        if (strcmp(processes[idx].stdin_file, "/dev/null") != 0) {
            int fd_in = open(processes[idx].stdin_file, O_RDONLY);
            if (fd_in == -1) {
                exit(1);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        } else {
            int fd_in = open("/dev/null", O_RDONLY);
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        
        // Перенаправление stdout
        int fd_out = open(processes[idx].stdout_file, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd_out == -1) {
            exit(1);
        }
        dup2(fd_out, STDOUT_FILENO);
        dup2(fd_out, STDERR_FILENO);
        close(fd_out);
        
        // Запуск программы
        execvp(processes[idx].argv[0], processes[idx].argv);
        exit(1);
    } else {
        processes[idx].pid = pid;
        processes[idx].active = 1;
        log_formatted("Started process %d: %s (pid %d)", idx, processes[idx].cmd, pid);
    }
}

char **parse_command_line(const char *cmd_str, int *argc) {
    char *str = strdup(cmd_str);
    if (!str) return NULL;
    
    char **argv = malloc(MAX_CMD_LEN * sizeof(char*));
    if (!argv) {
        free(str);
        return NULL;
    }
    
    *argc = 0;
    char *token = strtok(str, " ");
    while (token && *argc < MAX_CMD_LEN - 1) {
        argv[*argc] = strdup(token);
        (*argc)++;
        token = strtok(NULL, " ");
    }
    argv[*argc] = NULL;
    
    free(str);
    return argv;
}

int parse_config(const char *filename) {
    log_formatted("Attempting to open config file: %s", filename);
    
    FILE *config = fopen(filename, "r");
    if (!config) {
        log_formatted("Cannot open config file %s: %s", filename, strerror(errno));
        return -1;
    }

    // Очистка старых процессов
    for (int i = 0; i < num_procs; i++) {
        free_process(&processes[i]);
    }
    num_procs = 0;

    char line[CONFIG_LINE_MAX];
    int line_num = 0;
    
    while (fgets(line, sizeof(line), config) && num_procs < MAX_PROC) {
        line_num++;
        
        // Удаляем комментарии
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        
        // Удаляем пробелы в начале и конце
        char *start = line;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\n' || *start == '\0') continue;
        
        char *end = start + strlen(start) - 1;
        while (end > start && (*end == '\n' || *end == ' ' || *end == '\t')) end--;
        *(end + 1) = '\0';
        
        // Парсинг: команда [аргументы...] stdin stdout
        char *last_space = strrchr(start, ' ');
        if (!last_space) {
            log_formatted("Invalid config line %d: need at least 3 fields", line_num);
            continue;
        }
        
        char *second_last_space = start;
        char *tmp = start;
        int space_count = 0;
        
        while ((tmp = strchr(tmp, ' ')) != NULL) {
            space_count++;
            if (tmp == last_space) break;
            second_last_space = tmp;
            tmp++;
        }
        
        if (space_count < 2) {
            log_formatted("Invalid config line %d: need at least 3 fields", line_num);
            continue;
        }
        
        *second_last_space = '\0';
        char *stdin_file = second_last_space + 1;
        *last_space = '\0';
        char *stdout_file = last_space + 1;
        char *cmd = start;
        
        while (*stdin_file == ' ') stdin_file++;
        while (*stdout_file == ' ') stdout_file++;
        
        if (cmd[0] != '/') {
            log_formatted("Command path is not absolute (line %d): %s", line_num, cmd);
            continue;
        }
        if (stdin_file[0] != '/' && strcmp(stdin_file, "/dev/null") != 0) {
            log_formatted("Stdin path is not absolute (line %d): %s", line_num, stdin_file);
            continue;
        }
        if (stdout_file[0] != '/') {
            log_formatted("Stdout path is not absolute (line %d): %s", line_num, stdout_file);
            continue;
        }
        
        char first_arg[MAX_CMD_LEN];
        sscanf(cmd, "%s", first_arg);
        if (access(first_arg, X_OK) != 0) {
            log_formatted("Command not executable (line %d): %s", line_num, first_arg);
            continue;
        }
        
        processes[num_procs].cmd = strdup(cmd);
        processes[num_procs].stdin_file = strdup(stdin_file);
        processes[num_procs].stdout_file = strdup(stdout_file);
        processes[num_procs].argv = parse_command_line(cmd, &processes[num_procs].argc);
        processes[num_procs].active = 0;
        processes[num_procs].pid = -1;
        processes[num_procs].restart_disabled = 0;
        
        if (!processes[num_procs].argv) {
            log_formatted("Failed to parse command (line %d): %s", line_num, cmd);
            free_process(&processes[num_procs]);
            continue;
        }
        
        num_procs++;
    }
    
    fclose(config);
    log_formatted("Loaded %d processes from config file", num_procs);
    return 0;
}

void reload_config() {
    reloading = 1;
    
    log_message("SIGHUP received, reloading configuration");
    
    // Отключаем рестарт для всех процессов перед очисткой
    for (int i = 0; i < num_procs; i++) {
        processes[i].restart_disabled = 1;
    }
    
    cleanup_all_processes();
    
    if (parse_config(config_file_path) != 0) {
        log_message("Failed to reload config, keeping old configuration");
        reloading = 0;
        return;
    }
    
    for (int i = 0; i < num_procs; i++) {
        start_process(i);
    }
    
    log_message("Configuration reload completed");
    reloading = 0;
}

void daemonize() {
    pid_t pid;
    
    pid = fork();
    if (pid < 0) {
        perror("First fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    if (setsid() < 0) {
        perror("setsid failed");
        exit(EXIT_FAILURE);
    }
    
    pid = fork();
    if (pid < 0) {
        perror("Second fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    if (chdir("/") < 0) {
        perror("chdir failed");
        exit(EXIT_FAILURE);
    }
    
    int max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd == -1) max_fd = 1024;
    for (int fd = 0; fd < max_fd; fd++) {
        close(fd);
    }
    
    int fd0 = open("/dev/null", O_RDWR);
    if (fd0 < 0) {
        perror("open /dev/null failed");
        exit(EXIT_FAILURE);
    }
    dup2(fd0, STDIN_FILENO);
    
    int log_fd = open(log_file_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (log_fd < 0) {
        perror("open log file failed");
        exit(EXIT_FAILURE);
    }
    dup2(log_fd, STDOUT_FILENO);
    dup2(log_fd, STDERR_FILENO);
    
    close(log_fd);
    close(fd0);
    
    umask(022);
    
    log_message("myinit daemon started successfully");
    create_pid_file();
}

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        log_message("Received termination signal");
        if (!reloading) {
            running = 0;
        }
    } else if (sig == SIGHUP) {
        reload_config();
    } else if (sig == SIGCHLD) {
        int status;
        pid_t pid;
        
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < num_procs; i++) {
                if (processes[i].active && processes[i].pid == pid) {
                    if (WIFEXITED(status)) {
                        log_formatted("Process %d (%s pid %d) exited with status %d", 
                                      i, processes[i].cmd, pid, WEXITSTATUS(status));
                    } else if (WIFSIGNALED(status)) {
                        log_formatted("Process %d (%s pid %d) killed by signal %d", 
                                      i, processes[i].cmd, pid, WTERMSIG(status));
                    }
                    processes[i].active = 0;
                    
                    // Рестартим только если не запрещено и процесс ещё существует в конфиге
                    if (!processes[i].restart_disabled && i < num_procs && processes[i].cmd) {
                        start_process(i);
                    }
                    break;
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "c:")) != -1) {
        if (opt == 'c') {
            config_file_path = optarg;
        }
    }
    
    if (!config_file_path) {
        fprintf(stderr, "Usage: %s -c config_file\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    daemonize();
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
    
    if (parse_config(config_file_path) != 0) {
        log_message("Initial config load failed, exiting");
        exit(EXIT_FAILURE);
    }
    
    for (int i = 0; i < num_procs; i++) {
        start_process(i);
    }
    
    log_formatted("All %d processes started, entering monitor loop", num_procs);
    
    while (running) {
        pause();
    }
    
    log_message("Shutting down myinit daemon");
    cleanup_all_processes();
    remove_pid_file();
    
    for (int i = 0; i < num_procs; i++) {
        free_process(&processes[i]);
    }
    
    log_message("myinit daemon stopped");
    return 0;
}