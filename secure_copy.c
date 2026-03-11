#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "caesar.h"

#define BUFFER_SIZE 4096
#define MAX_FILENAME 256

volatile int keep_running = 1;

typedef struct {
    unsigned char buffer[BUFFER_SIZE];
    int size;
    int done;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} Buffer;

/* arguments passed to both producer and consumer threads */
typedef struct {
    Buffer *buf;
    int input_fd;
    int output_fd;
    char key;
} ThreadArgs;

void sigint_handler(int sig) {
    keep_running = 0;
    printf("\nОперация прервана пользователем\n");
}

void* producer(void* arg) {
    ThreadArgs *args = (ThreadArgs*)arg;
    Buffer *shared_buffer = args->buf;
    int fd = args->input_fd;
    char key = args->key;

    unsigned char data[BUFFER_SIZE];
    ssize_t bytes_read;

    while (keep_running) {
        bytes_read = read(fd, data, BUFFER_SIZE);
        if (bytes_read <= 0) {
            break;
        }

        set_key(key);
        caesar(data, data, bytes_read);

        pthread_mutex_lock(&shared_buffer->mutex);
        while (shared_buffer->size > 0 && keep_running) {
            pthread_cond_wait(&shared_buffer->not_full, &shared_buffer->mutex);
        }
        if (!keep_running) {
            pthread_mutex_unlock(&shared_buffer->mutex);
            break;
        }
        memcpy(shared_buffer->buffer, data, bytes_read);
        shared_buffer->size = bytes_read;
        shared_buffer->done = (bytes_read < BUFFER_SIZE);
        pthread_cond_signal(&shared_buffer->not_empty);
        pthread_mutex_unlock(&shared_buffer->mutex);
    }

    pthread_mutex_lock(&shared_buffer->mutex);
    shared_buffer->done = 1;
    pthread_cond_signal(&shared_buffer->not_empty);
    pthread_mutex_unlock(&shared_buffer->mutex);

    return NULL;
}

void* consumer(void* arg) {
    ThreadArgs *args = (ThreadArgs*)arg;
    Buffer *shared_buffer = args->buf;
    int fd = args->output_fd;

    while (keep_running) {
        pthread_mutex_lock(&shared_buffer->mutex);
        while (shared_buffer->size == 0 && !shared_buffer->done && keep_running) {
            pthread_cond_wait(&shared_buffer->not_empty, &shared_buffer->mutex);
        }
        if (!keep_running || (shared_buffer->size == 0 && shared_buffer->done)) {
            pthread_mutex_unlock(&shared_buffer->mutex);
            break;
        }
        write(fd, shared_buffer->buffer, shared_buffer->size);
        shared_buffer->size = 0;
        pthread_cond_signal(&shared_buffer->not_full);
        pthread_mutex_unlock(&shared_buffer->mutex);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Использование: %s <входной_файл> <выходной_файл> <ключ>\n", argv[0]);
        return 1;
    }

    char* input_file = argv[1];
    char* output_file = argv[2];
    int key = atoi(argv[3]);

    if (key < 0 || key > 255) {
        fprintf(stderr, "Ключ должен быть от 0 до 255\n");
        return 1;
    }

    int input_fd = open(input_file, O_RDONLY);
    if (input_fd == -1) {
        perror("Ошибка открытия входного файла");
        return 1;
    }

    int output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd == -1) {
        perror("Ошибка открытия выходного файла");
        close(input_fd);
        return 1;
    }

    Buffer shared_buffer;
    shared_buffer.size = 0;
    shared_buffer.done = 0;
    pthread_mutex_init(&shared_buffer.mutex, NULL);
    pthread_cond_init(&shared_buffer.not_empty, NULL);
    pthread_cond_init(&shared_buffer.not_full, NULL);

    signal(SIGINT, sigint_handler);

    ThreadArgs args;
    args.buf = &shared_buffer;
    args.input_fd = input_fd;
    args.output_fd = output_fd;
    args.key = (char)key;

    pthread_t prod_thread, cons_thread;
    pthread_create(&prod_thread, NULL, producer, &args);
    pthread_create(&cons_thread, NULL, consumer, &args);

    pthread_join(prod_thread, NULL);
    pthread_join(cons_thread, NULL);

    close(input_fd);
    close(output_fd);

    pthread_mutex_destroy(&shared_buffer.mutex);
    pthread_cond_destroy(&shared_buffer.not_empty);
    pthread_cond_destroy(&shared_buffer.not_full);

    return 0;
}
