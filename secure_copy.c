#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include "caesar.h"

#define BUFFER_SIZE 4096
#define MAX_FILENAME 256
#define TIMEOUT_SEC 5

typedef struct {
    char **files;
    int num_files;
    char *output_dir;
    char key;
    int *current_index;
    int *processed_count;
    pthread_mutex_t *mutex;
    FILE *log_file;
} WorkerArgs;

void log_operation(FILE *log_file, pthread_mutex_t *mutex, const char *filename, const char *result, double duration) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[26];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Используем отдельный захват мьютекса для логирования
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += TIMEOUT_SEC;
    
    int ret = pthread_mutex_timedlock(mutex, &timeout);
    if (ret == 0) {
        fprintf(log_file, "[%s] Thread %lu: %s - %s (%.3f sec)\n", 
                time_str, (unsigned long)pthread_self(), filename, result, duration);
        fflush(log_file);
        pthread_mutex_unlock(mutex);
    } else if (ret == ETIMEDOUT) {
        fprintf(stderr, "Log timeout for %s\n", filename);
    }
}

void* worker(void* arg) {
    WorkerArgs *args = (WorkerArgs*)arg;
    
    while (1) {
        // Получаем индекс файла для обработки
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += TIMEOUT_SEC;
        
        int ret = pthread_mutex_timedlock(args->mutex, &timeout);
        if (ret == ETIMEDOUT) {
            printf("Возможная взаимоблокировка: поток %lu ожидает мьютекс более %d секунд\n", 
                   (unsigned long)pthread_self(), TIMEOUT_SEC);
            continue;
        } else if (ret != 0) {
            break;
        }
        
        // Проверяем, есть ли еще файлы для обработки
        if (*args->current_index >= args->num_files) {
            pthread_mutex_unlock(args->mutex);
            break;
        }
        
        // Получаем имя файла и увеличиваем индекс
        char *filename = args->files[*args->current_index];
        (*args->current_index)++;
        pthread_mutex_unlock(args->mutex);

        // Обрабатываем файл
        time_t start_time = time(NULL);
        char result[20] = "success";
        
        // Формируем полный путь к выходному файлу
        char out_path[MAX_FILENAME];
        // Извлекаем только имя файла из пути (если есть)
        const char *base_filename = strrchr(filename, '/');
        if (base_filename == NULL) {
            base_filename = filename;
        } else {
            base_filename++; // Пропускаем '/'
        }
        snprintf(out_path, sizeof(out_path), "%s/%s", args->output_dir, base_filename);
        
        // Открываем входной файл
        FILE *in_file = fopen(filename, "rb");
        if (in_file == NULL) {
            snprintf(result, sizeof(result), "error: open");
            log_operation(args->log_file, args->mutex, filename, result, difftime(time(NULL), start_time));
            continue;
        }
        
        // Определяем размер файла
        fseek(in_file, 0, SEEK_END);
        long file_size = ftell(in_file);
        fseek(in_file, 0, SEEK_SET);
        
        if (file_size <= 0) {
            fclose(in_file);
            snprintf(result, sizeof(result), "error: empty");
            log_operation(args->log_file, args->mutex, filename, result, difftime(time(NULL), start_time));
            continue;
        }
        
        // Читаем файл
        unsigned char *buffer = (unsigned char*)malloc(file_size);
        if (buffer == NULL) {
            fclose(in_file);
            snprintf(result, sizeof(result), "error: memory");
            log_operation(args->log_file, args->mutex, filename, result, difftime(time(NULL), start_time));
            continue;
        }
        
        size_t bytes_read = fread(buffer, 1, file_size, in_file);
        fclose(in_file);
        
        if (bytes_read != file_size) {
            free(buffer);
            snprintf(result, sizeof(result), "error: read");
            log_operation(args->log_file, args->mutex, filename, result, difftime(time(NULL), start_time));
            continue;
        }
        
        // Шифруем данные
        set_key(args->key);
        caesar(buffer, buffer, file_size);
        
        // Записываем зашифрованные данные
        FILE *out_file = fopen(out_path, "wb");
        if (out_file == NULL) {
            free(buffer);
            snprintf(result, sizeof(result), "error: write");
            log_operation(args->log_file, args->mutex, filename, result, difftime(time(NULL), start_time));
            continue;
        }
        
        size_t bytes_written = fwrite(buffer, 1, file_size, out_file);
        fclose(out_file);
        free(buffer);
        
        if (bytes_written != file_size) {
            snprintf(result, sizeof(result), "error: write");
        }
        
        // Логируем успешную операцию
        double duration = difftime(time(NULL), start_time);
        log_operation(args->log_file, args->mutex, filename, result, duration);
        
        // Обновляем счетчик обработанных файлов
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += TIMEOUT_SEC;
        ret = pthread_mutex_timedlock(args->mutex, &timeout);
        if (ret == 0) {
            (*args->processed_count)++;
            pthread_mutex_unlock(args->mutex);
        }
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s file1 file2 ... output_dir key\n", argv[0]);
        fprintf(stderr, "Example: %s file1.txt file2.txt file3.txt output_dir 3\n", argv[0]);
        return 1;
    }
    
    // Парсим аргументы
    int num_files = argc - 3;
    char *output_dir = argv[argc - 2];
    char key = (char)atoi(argv[argc - 1]);
    char **files = &argv[1];
    
    printf("Starting secure_copy with %d files, output_dir=%s, key=%d\n", 
           num_files, output_dir, key);
    
    // Создаем выходную директорию
    struct stat st = {0};
    if (stat(output_dir, &st) == -1) {
        if (mkdir(output_dir, 0755) != 0) {
            perror("mkdir");
            return 1;
        }
        printf("Created output directory: %s\n", output_dir);
    }
    
    // Открываем лог-файл в режиме добавления
    FILE *log_file = fopen("log.txt", "a");
    if (!log_file) {
        perror("fopen log.txt");
        return 1;
    }
    printf("Log file opened: log.txt\n");
    
    // Инициализируем мьютекс
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    
    // Общие переменные
    int current_index = 0;
    int processed_count = 0;
    
    // Аргументы для рабочих потоков
    WorkerArgs args = {
        .files = files,
        .num_files = num_files,
        .output_dir = output_dir,
        .key = key,
        .current_index = &current_index,
        .processed_count = &processed_count,
        .mutex = &mutex,
        .log_file = log_file
    };
    
    // Создаем 3 рабочих потока
    pthread_t threads[3];
    for (int i = 0; i < 3; i++) {
        if (pthread_create(&threads[i], NULL, worker, &args) != 0) {
            perror("pthread_create");
            fclose(log_file);
            return 1;
        }
        printf("Created thread %d\n", i);
    }
    
    // Ожидаем завершения всех потоков
    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
        printf("Thread %d joined\n", i);
    }
    
    // Закрываем лог-файл
    fclose(log_file);
    
    printf("All files processed. Total: %d files\n", processed_count);
    printf("Check log.txt for details\n");
    
    return 0;
}