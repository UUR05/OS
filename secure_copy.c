#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include "caesar.h"

#define MAX_WORKERS 4
#define MAX_PATH_LEN 1024

typedef enum {
    MODE_AUTO = 0,
    MODE_SEQUENTIAL,
    MODE_PARALLEL
} RunMode;

typedef struct {
    double start_ms;
    double end_ms;
    double duration_ms;
    int ok;
} FileStat;

typedef struct {
    double total_ms;
    double avg_ms;
    int processed;
    int success;
    int failed;
} RunStats;

typedef struct {
    int *indices;
    int total;
    int head;
    int tail;
    int count;
    int closed;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} FileQueue;

typedef struct {
    FileQueue *queue;
    char **files;
    const char *output_dir;
    FileStat *stats;
    struct timespec run_start;
    int *success_count;
    int *failed_count;
    pthread_mutex_t *stats_mutex;
} WorkerContext;

static double diff_ms(struct timespec start, struct timespec end) {
    double seconds = (double)(end.tv_sec - start.tv_sec) * 1000.0;
    double nseconds = (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
    return seconds + nseconds;
}

static const char *basename_from_path(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *last = slash;
    if (backslash != NULL && (last == NULL || backslash > last)) {
        last = backslash;
    }
    return last == NULL ? path : last + 1;
}

static void build_output_path(const char *output_dir, const char *input_file, char *out_path, size_t out_size) {
    const char *base = basename_from_path(input_file);
    snprintf(out_path, out_size, "%s/%s", output_dir, base);
}

static int ensure_output_dir(const char *output_dir) {
    struct stat st;
    if (stat(output_dir, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "Path exists but is not a directory: %s\n", output_dir);
            return -1;
        }
        return 0;
    }

    if (mkdir(output_dir, 0755) != 0) {
        perror("mkdir");
        return -1;
    }
    return 0;
}

static int process_one_file(const char *input_file, const char *output_dir, const struct timespec *run_start,
                            double *start_ms, double *end_ms, double *duration_ms) {
    FILE *in = NULL;
    FILE *out = NULL;
    unsigned char *buffer = NULL;
    long file_size;
    size_t bytes_read;
    size_t bytes_written;
    char out_path[MAX_PATH_LEN];
    struct timespec start;
    struct timespec end;
    int status = -1;

    clock_gettime(CLOCK_MONOTONIC, &start);

    in = fopen(input_file, "rb");
    if (in == NULL) {
        perror(input_file);
        goto cleanup;
    }

    if (fseek(in, 0, SEEK_END) != 0) {
        perror("fseek end");
        goto cleanup;
    }
    file_size = ftell(in);
    if (file_size < 0) {
        perror("ftell");
        goto cleanup;
    }
    if (fseek(in, 0, SEEK_SET) != 0) {
        perror("fseek set");
        goto cleanup;
    }

    buffer = (unsigned char *)malloc((size_t)file_size);
    if (buffer == NULL && file_size > 0) {
        perror("malloc");
        goto cleanup;
    }

    bytes_read = fread(buffer, 1, (size_t)file_size, in);
    if (bytes_read != (size_t)file_size) {
        perror("fread");
        goto cleanup;
    }

    caesar(buffer, buffer, (int)file_size);

    build_output_path(output_dir, input_file, out_path, sizeof(out_path));
    out = fopen(out_path, "wb");
    if (out == NULL) {
        perror(out_path);
        goto cleanup;
    }

    bytes_written = fwrite(buffer, 1, (size_t)file_size, out);
    if (bytes_written != (size_t)file_size) {
        perror("fwrite");
        goto cleanup;
    }

    status = 0;

cleanup:
    if (out != NULL) {
        fclose(out);
    }
    if (in != NULL) {
        fclose(in);
    }
    free(buffer);

    clock_gettime(CLOCK_MONOTONIC, &end);
    *start_ms = diff_ms(*run_start, start);
    *end_ms = diff_ms(*run_start, end);
    *duration_ms = diff_ms(start, end);
    return status;
}

static int queue_init(FileQueue *queue, int total) {
    queue->indices = (int *)malloc((size_t)total * sizeof(int));
    if (queue->indices == NULL) {
        return -1;
    }
    queue->total = total;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->closed = 0;

    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&queue->cond, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    return 0;
}

static void queue_destroy(FileQueue *queue) {
    pthread_cond_destroy(&queue->cond);
    pthread_mutex_destroy(&queue->mutex);
    free(queue->indices);
}

static int queue_push(FileQueue *queue, int file_index) {
    if (queue->count >= queue->total) {
        return -1;
    }

    queue->indices[queue->tail] = file_index;
    queue->tail = (queue->tail + 1) % queue->total;
    queue->count++;
    return 0;
}

static int queue_pop(FileQueue *queue) {
    int result;
    if (queue->count == 0) {
        return -1;
    }

    result = queue->indices[queue->head];
    queue->head = (queue->head + 1) % queue->total;
    queue->count--;
    return result;
}

static void *worker_thread(void *arg) {
    WorkerContext *ctx = (WorkerContext *)arg;

    while (1) {
        int file_index;
        char *file;
        double start_ms;
        double end_ms;
        double duration_ms;
        int ok;

        pthread_mutex_lock(&ctx->queue->mutex);
        while (ctx->queue->count == 0 && !ctx->queue->closed) {
            pthread_cond_wait(&ctx->queue->cond, &ctx->queue->mutex);
        }

        if (ctx->queue->count == 0 && ctx->queue->closed) {
            pthread_mutex_unlock(&ctx->queue->mutex);
            break;
        }

        file_index = queue_pop(ctx->queue);
        pthread_mutex_unlock(&ctx->queue->mutex);

        if (file_index < 0) {
            continue;
        }

        file = ctx->files[file_index];

        ok = (process_one_file(file, ctx->output_dir, &ctx->run_start, &start_ms, &end_ms, &duration_ms) == 0);
        ctx->stats[file_index].start_ms = start_ms;
        ctx->stats[file_index].end_ms = end_ms;
        ctx->stats[file_index].duration_ms = duration_ms;
        ctx->stats[file_index].ok = ok;

        pthread_mutex_lock(ctx->stats_mutex);
        if (ok) {
            (*ctx->success_count)++;
        } else {
            (*ctx->failed_count)++;
        }
        pthread_mutex_unlock(ctx->stats_mutex);
    }

    return NULL;
}

static RunStats run_sequential(char **files, int num_files, const char *output_dir) {
    struct timespec start;
    struct timespec end;
    RunStats stats;
    int i;

    memset(&stats, 0, sizeof(stats));
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < num_files; i++) {
        double start_ms = 0.0;
        double end_ms = 0.0;
        double duration_ms = 0.0;
        int ok = (process_one_file(files[i], output_dir, &start, &start_ms, &end_ms, &duration_ms) == 0);
        stats.processed++;
        if (ok) {
            stats.success++;
        } else {
            stats.failed++;
        }
        printf("[sequential] file=%s start=%.3f ms end=%.3f ms duration=%.3f ms status=%s\n", files[i], start_ms,
               end_ms, duration_ms, ok ? "ok" : "error");
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    stats.total_ms = diff_ms(start, end);
    stats.avg_ms = (num_files > 0) ? stats.total_ms / (double)num_files : 0.0;
    return stats;
}

static RunStats run_parallel(char **files, int num_files, const char *output_dir) {
    FileQueue queue;
    WorkerContext ctx;
    pthread_t workers[MAX_WORKERS];
    FileStat *per_file_stats;
    pthread_mutex_t stats_mutex;
    struct timespec start;
    struct timespec end;
    int worker_count;
    int success_count = 0;
    int failed_count = 0;
    int i;
    RunStats stats;

    memset(&stats, 0, sizeof(stats));
    if (num_files <= 0) {
        return stats;
    }

    per_file_stats = (FileStat *)calloc((size_t)num_files, sizeof(FileStat));
    if (per_file_stats == NULL) {
        perror("calloc");
        return stats;
    }

    if (queue_init(&queue, num_files) != 0) {
        perror("queue_init");
        free(per_file_stats);
        return stats;
    }

    if (pthread_mutex_init(&stats_mutex, NULL) != 0) {
        perror("pthread_mutex_init");
        queue_destroy(&queue);
        free(per_file_stats);
        return stats;
    }

    worker_count = (num_files < MAX_WORKERS) ? num_files : MAX_WORKERS;
    ctx.queue = &queue;
    ctx.files = files;
    ctx.output_dir = output_dir;
    ctx.stats = per_file_stats;
    ctx.success_count = &success_count;
    ctx.failed_count = &failed_count;
    ctx.stats_mutex = &stats_mutex;

    clock_gettime(CLOCK_MONOTONIC, &start);
    ctx.run_start = start;

    for (i = 0; i < worker_count; i++) {
        if (pthread_create(&workers[i], NULL, worker_thread, &ctx) != 0) {
            perror("pthread_create");
            queue.closed = 1;
            pthread_cond_broadcast(&queue.cond);
            worker_count = i;
            break;
        }
    }

    pthread_mutex_lock(&queue.mutex);
    for (i = 0; i < num_files; i++) {
        if (queue_push(&queue, i) != 0) {
            fprintf(stderr, "Queue push failed for %s\n", files[i]);
        }
    }
    queue.closed = 1;
    pthread_cond_broadcast(&queue.cond);
    pthread_mutex_unlock(&queue.mutex);

    for (i = 0; i < worker_count; i++) {
        pthread_join(workers[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    stats.processed = num_files;
    stats.success = success_count;
    stats.failed = failed_count;
    stats.total_ms = diff_ms(start, end);
    stats.avg_ms = (num_files > 0) ? stats.total_ms / (double)num_files : 0.0;

    for (i = 0; i < num_files; i++) {
        printf("[parallel] file=%s start=%.3f ms end=%.3f ms duration=%.3f ms status=%s\n", files[i],
               per_file_stats[i].start_ms, per_file_stats[i].end_ms, per_file_stats[i].duration_ms,
               per_file_stats[i].ok ? "ok" : "error");
    }

    pthread_mutex_destroy(&stats_mutex);
    queue_destroy(&queue);
    free(per_file_stats);
    return stats;
}

static const char *mode_to_string(RunMode mode) {
    if (mode == MODE_SEQUENTIAL) {
        return "sequential";
    }
    if (mode == MODE_PARALLEL) {
        return "parallel";
    }
    return "auto";
}

static void print_stats(const char *title, RunMode mode, RunStats stats) {
    printf("\n=== %s ===\n", title);
    printf("mode: %s\n", mode_to_string(mode));
    printf("files processed: %d\n", stats.processed);
    printf("successful: %d\n", stats.success);
    printf("failed: %d\n", stats.failed);
    printf("total time: %.3f ms\n", stats.total_ms);
    printf("average per file: %.3f ms\n", stats.avg_ms);
}

static void print_comparison(RunMode selected_mode, RunStats selected_stats, RunMode alt_mode, RunStats alt_stats) {
    printf("\n=== Auto Mode Comparison ===\n");
    printf("+---------------+---------------+----------------+\n");
    printf("| mode          | total (ms)    | avg/file (ms)  |\n");
    printf("+---------------+---------------+----------------+\n");
    printf("| %-13s | %13.3f | %14.3f |\n", mode_to_string(selected_mode), selected_stats.total_ms,
           selected_stats.avg_ms);
    printf("| %-13s | %13.3f | %14.3f |\n", mode_to_string(alt_mode), alt_stats.total_ms, alt_stats.avg_ms);
    printf("+---------------+---------------+----------------+\n");
}

static int parse_mode(const char *arg, RunMode *mode) {
    const char *prefix = "--mode=";
    if (strncmp(arg, prefix, strlen(prefix)) != 0) {
        return -1;
    }

    arg += strlen(prefix);
    if (strcmp(arg, "sequential") == 0) {
        *mode = MODE_SEQUENTIAL;
        return 0;
    }
    if (strcmp(arg, "parallel") == 0) {
        *mode = MODE_PARALLEL;
        return 0;
    }
    if (strcmp(arg, "auto") == 0) {
        *mode = MODE_AUTO;
        return 0;
    }
    return -1;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [--mode=sequential|parallel|auto] file1 file2 ... output_dir key\n", prog);
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s --mode=sequential f1.txt f2.txt out 7\n", prog);
    fprintf(stderr, "  %s --mode=parallel f1.txt f2.txt f3.txt f4.txt f5.txt out 7\n", prog);
    fprintf(stderr, "  %s f1.txt f2.txt f3.txt out 7\n", prog);
}

int main(int argc, char *argv[]) {
    int arg_offset = 1;
    RunMode requested_mode = MODE_AUTO;
    RunMode selected_mode;
    int num_files;
    char **files;
    const char *output_dir;
    int key;
    RunStats primary_stats;

    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    if (strncmp(argv[1], "--mode=", 7) == 0) {
        if (parse_mode(argv[1], &requested_mode) != 0) {
            fprintf(stderr, "Invalid mode: %s\n", argv[1]);
            print_usage(argv[0]);
            return 1;
        }
        arg_offset = 2;
    }

    if (argc - arg_offset < 3) {
        print_usage(argv[0]);
        return 1;
    }

    num_files = argc - arg_offset - 2;
    files = &argv[arg_offset];
    output_dir = argv[argc - 2];
    key = atoi(argv[argc - 1]);

    if (num_files <= 0) {
        fprintf(stderr, "At least one input file is required.\n");
        return 1;
    }

    if (ensure_output_dir(output_dir) != 0) {
        return 1;
    }

    set_key((char)key);

    if (requested_mode == MODE_AUTO) {
        selected_mode = (num_files < 5) ? MODE_SEQUENTIAL : MODE_PARALLEL;
    } else {
        selected_mode = requested_mode;
    }

    printf("Requested mode: %s\n", mode_to_string(requested_mode));
    printf("Selected mode: %s\n", mode_to_string(selected_mode));
    printf("Input files: %d\n", num_files);

    if (selected_mode == MODE_SEQUENTIAL) {
        primary_stats = run_sequential(files, num_files, output_dir);
    } else {
        primary_stats = run_parallel(files, num_files, output_dir);
    }

    print_stats("Run Statistics", selected_mode, primary_stats);

    if (requested_mode == MODE_AUTO) {
        RunMode alt_mode = (selected_mode == MODE_SEQUENTIAL) ? MODE_PARALLEL : MODE_SEQUENTIAL;
        RunStats alt_stats;

        if (alt_mode == MODE_SEQUENTIAL) {
            alt_stats = run_sequential(files, num_files, output_dir);
        } else {
            alt_stats = run_parallel(files, num_files, output_dir);
        }

        print_stats("Alternative Mode Statistics", alt_mode, alt_stats);
        print_comparison(selected_mode, primary_stats, alt_mode, alt_stats);
    }

    return (primary_stats.failed == 0) ? 0 : 2;
}