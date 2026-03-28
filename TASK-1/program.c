#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <getopt.h>

#define DEFAULT_BLOCK_SIZE 4096

void print_error_and_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [options] output_file\n", prog_name);
    fprintf(stderr, "  %s [options] input_file output_file\n", prog_name);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -b, --block-size SIZE  Set block size in bytes (default: 4096)\n");
    fprintf(stderr, "  -h, --help            Show this help message\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s fileB                    # read from stdin, write to fileB\n", prog_name);
    fprintf(stderr, "  %s fileA fileB              # copy fileA to fileB with sparse support\n", prog_name);
    fprintf(stderr, "  %s -b 100 fileA fileB       # use block size 100 bytes\n", prog_name);
}

void copy_with_sparse(int src_fd, int dst_fd, size_t block_size) {
    char *buffer = malloc(block_size);
    if (!buffer) {
        print_error_and_exit("malloc failed");
    }

    ssize_t bytes_read;
    off_t zero_run_length = 0;
    off_t current_pos = 0;

    while ((bytes_read = read(src_fd, buffer, block_size)) > 0) {
        // Проверяем, состоит ли блок полностью из нулей
        int is_zero_block = 1;
        for (ssize_t i = 0; i < bytes_read; i++) {
            if (buffer[i] != 0) {
                is_zero_block = 0;
                break;
            }
        }

        if (is_zero_block && bytes_read == (ssize_t)block_size) {
            // Полностью нулевой блок — пропускаем запись
            zero_run_length += bytes_read;
        } else {
            // Записываем предыдущий нулевой прогон, если он был
            if (zero_run_length > 0) {
                if (lseek(dst_fd, zero_run_length, SEEK_CUR) == (off_t)-1) {
                    print_error_and_exit("lseek failed");
                }
                zero_run_length = 0;
            }

            // Записываем текущий блок (может быть частичным в конце)
            if (write(dst_fd, buffer, bytes_read) != bytes_read) {
                print_error_and_exit("write failed");
            }
        }
        current_pos += bytes_read;
    }

    if (bytes_read == -1) {
        print_error_and_exit("read failed");
    }

    // Обработка нулевого прогона в конце файла
    if (zero_run_length > 0) {
        if (lseek(dst_fd, zero_run_length, SEEK_CUR) == (off_t)-1) {
            print_error_and_exit("lseek failed");
        }
    }

    free(buffer);
}

int main(int argc, char *argv[]) {
    int src_fd, dst_fd;
    size_t block_size = DEFAULT_BLOCK_SIZE;
    int opt;
    int option_index = 0;
    
    // Опции для getopt_long
    static struct option long_options[] = {
        {"block-size", required_argument, 0, 'b'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    // Разбор аргументов с помощью getopt_long
    while ((opt = getopt_long(argc, argv, "b:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'b':
                block_size = atoi(optarg);
                if (block_size == 0) {
                    fprintf(stderr, "Error: invalid block size '%s'\n", optarg);
                    print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                break;
                
            case 'h':
                print_usage(argv[0]);
                exit(EXIT_SUCCESS);
                
            case '?':
                // getopt_long уже вывел сообщение об ошибке
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
                
            default:
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    
    // Проверка количества позиционных аргументов
    int remaining_args = argc - optind;
    const char *src_name = NULL;
    const char *dst_name = NULL;
    
    if (remaining_args == 1) {
        // Один аргумент — читаем stdin
        src_name = NULL;
        dst_name = argv[optind];
    } else if (remaining_args == 2) {
        // Два аргумента — читаем из файла
        src_name = argv[optind];
        dst_name = argv[optind + 1];
    } else {
        fprintf(stderr, "Error: invalid number of arguments\n");
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Проверка, что имена файлов не пустые
    if (dst_name == NULL || strlen(dst_name) == 0) {
        fprintf(stderr, "Error: output file name is required\n");
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Открытие исходного файла
    if (src_name == NULL) {
        src_fd = STDIN_FILENO;
    } else {
        src_fd = open(src_name, O_RDONLY);
        if (src_fd == -1) {
            print_error_and_exit("open source file failed");
        }
    }
    
    // Открытие выходного файла
    dst_fd = open(dst_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd == -1) {
        print_error_and_exit("open destination file failed");
    }
    
    // Копирование с созданием разреженного файла
    copy_with_sparse(src_fd, dst_fd, block_size);
    
    // Закрытие файлов
    if (src_name != NULL && close(src_fd) == -1) {
        print_error_and_exit("close source failed");
    }
    if (close(dst_fd) == -1) {
        print_error_and_exit("close destination failed");
    }
    
    return 0;
}