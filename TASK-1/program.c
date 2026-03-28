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

void print_error(const char *msg) {
    perror(msg);
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [options] output_file\n", prog_name);
    fprintf(stderr, "  %s [options] input_file output_file\n", prog_name);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -b, --block-size SIZE  Set block size in bytes (default: 4096)\n");
    fprintf(stderr, "  -h, --help            Show this help message\n");
}

int copy_with_sparse(int source_file_descriptor, int destination_file_descriptor, size_t block_size, char *buffer, char *zero_buffer) {
    ssize_t bytes_read;
    off_t zero_run_length = 0;
    off_t current_pos = 0;

    
    while ((bytes_read = read(source_file_descriptor, buffer, block_size)) > 0) {
        
        int is_zero_block;

        // Проверяем, состоит ли блок полностью из нулей
        // Для маленьких блоков используем простой цикл (меньше накладных расходов)
        if (bytes_read <= 64) {
            is_zero_block = 1;
            for (ssize_t i = 0; i < bytes_read; i++) {
                if (buffer[i] != 0) {
                    is_zero_block = 0;
                    break;
                }
            }
        } else {
            // Для больших блоков используем memcmp (оптимизирован библиотекой)
            is_zero_block = (memcmp(buffer, zero_buffer, bytes_read) == 0);
        }

        if (is_zero_block) {
            zero_run_length += bytes_read;
        } else {
            // Записываем предыдущий нулевой прогон, если он был
            if (zero_run_length > 0) {
                if (lseek(destination_file_descriptor, zero_run_length, SEEK_CUR) == (off_t)-1) {
                    print_error("lseek failed");
                    return -1;
                }
                zero_run_length = 0;
            }

            // Записываем текущий блок (может быть частичным в конце)
            if (write(destination_file_descriptor, buffer, bytes_read) != bytes_read) {
                print_error("write failed");
                return -1;
            }
        }
        current_pos += bytes_read;
    }

    if (bytes_read == -1) {
        print_error("read failed");
        return -1;
    }

    // исправляет размер файла, если последние блоки были нулевыми и мы их пропустили
    if (ftruncate(destination_file_descriptor, current_pos) == -1) {
        print_error("ftruncate failed");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int source_file_descriptor = -1;
    int destination_file_descriptor = -1;
    size_t block_size = DEFAULT_BLOCK_SIZE;
    int opt;
    int exit_status = EXIT_SUCCESS;
    
    static struct option long_options[] = {
        {"block-size", required_argument, 0, 'b'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "b:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'b':
                block_size = strtoul(optarg, NULL, 10);
                // Размер блока должен быть > 0 и не превышать 1 ГБ
                // (Ограничение на 1гб сделал искусственно, чтобы программа не зависала и не убивалась ом киллером)
                if (optarg[0] == '-' || block_size == 0 || block_size > 1024 * 1024 * 1024) {
                    fprintf(stderr, "Error: invalid block size '%s'\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                return EXIT_FAILURE;
        }
    }
    
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
        return EXIT_FAILURE;
    }
    
    if (dst_name == NULL || strlen(dst_name) == 0) {
        fprintf(stderr, "Error: output file name is required\n");
        return EXIT_FAILURE;
    }
    
    char *buffer = malloc(block_size);
    // Для оптимизированной проверки нулевых блоков используем статический нулевой буфер
    char *zero_buffer = calloc(1, block_size);

    if (!buffer || !zero_buffer) {
        fprintf(stderr, "Error: memory allocation failed\n");
        free(buffer);
        free(zero_buffer);
        return EXIT_FAILURE;
    }

    if (src_name == NULL) {
        source_file_descriptor = STDIN_FILENO;
    } else {
        source_file_descriptor = open(src_name, O_RDONLY);
        if (source_file_descriptor == -1) {
            print_error("open source file failed");
            exit_status = EXIT_FAILURE;
            goto cleanup;
        }
    }
    
    destination_file_descriptor = open(dst_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (destination_file_descriptor == -1) {
        print_error("open destination file failed");
        exit_status=EXIT_FAILURE;
        goto cleanup;
    }
    
    if (copy_with_sparse(source_file_descriptor, destination_file_descriptor, block_size, buffer, zero_buffer) != 0){
        fprintf(stderr, "An error occured during copying");
        exit_status = EXIT_FAILURE; 
    }
    
cleanup:
    // освобождение памяти буфферов и закрытие файлов
    free(buffer);
    free(zero_buffer);

    if (src_name != NULL && source_file_descriptor!=-1) {
        if (close(source_file_descriptor) == -1){
            print_error("close source failed");
        }
    }
    if (destination_file_descriptor != -1) {
        if (close(destination_file_descriptor) == -1){
            print_error("close destination failed");
        }
    }
    
    return exit_status;
}