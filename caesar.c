#include "caesar.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

enum { KEY_STORAGE_SIZE = 16 };

static unsigned char *protected_key = NULL;
static int cleanup_registered = 0;
static int segv_handler_registered = 0;

static void segv_handler(int signo) {
    (void)signo;
    const char message[] = "security error: protected key access fault\n";
    write(STDERR_FILENO, message, sizeof(message) - 1);
    _Exit(EXIT_FAILURE);
}

static void install_segv_handler(void) {
    if (segv_handler_registered) {
        return;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = segv_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;

    if (sigaction(SIGSEGV, &action, NULL) != 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    segv_handler_registered = 1;
}

static void ensure_key_storage(void) {
    if (protected_key != NULL) {
        return;
    }

    install_segv_handler();

    protected_key = (unsigned char *)mmap(NULL, KEY_STORAGE_SIZE, PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (protected_key == MAP_FAILED) {
        protected_key = NULL;
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    memset(protected_key, 0, KEY_STORAGE_SIZE);

    if (!cleanup_registered && atexit(destroy_key) != 0) {
        fprintf(stderr, "failed to register key cleanup\n");
        destroy_key();
        exit(EXIT_FAILURE);
    }

    cleanup_registered = 1;
}

static void set_key_protection(int prot) {
    if (protected_key == NULL) {
        return;
    }

    if (mprotect(protected_key, KEY_STORAGE_SIZE, prot) != 0) {
        perror("mprotect");
        destroy_key();
        exit(EXIT_FAILURE);
    }
}

static unsigned char read_protected_key(void) {
    unsigned char key_byte = 0;

    ensure_key_storage();
    set_key_protection(PROT_READ | PROT_WRITE);
    memcpy(&key_byte, protected_key, sizeof(key_byte));
    set_key_protection(PROT_READ);

    return key_byte;
}

/**
 * Устанавливает ключ шифрования.
 * @param key Ключ шифрования (один байт)
 */
void set_key(char key) {
    ensure_key_storage();
    set_key_protection(PROT_READ | PROT_WRITE);
    memset(protected_key, 0, KEY_STORAGE_SIZE);
    memcpy(protected_key, &key, sizeof(key));
    set_key_protection(PROT_READ);
}

/**
 * Clears and releases the protected key storage.
 */
void destroy_key(void) {
    if (protected_key == NULL) {
        return;
    }

    set_key_protection(PROT_READ | PROT_WRITE);
    memset(protected_key, 0, KEY_STORAGE_SIZE);
    if (munmap(protected_key, KEY_STORAGE_SIZE) != 0) {
        perror("munmap");
    }

    protected_key = NULL;
}

/**
 * Выполняет XOR-шифрование/дешифрование.
 * @param src Указатель на исходный буфер
 * @param dst Указатель на буфер назначения
 * @param len Количество байт для обработки
 */
void caesar(void* src, void* dst, int len) {
    if (src == NULL || dst == NULL || len < 0) {
        return;
    }
    
    unsigned char* src_bytes = (unsigned char*)src;
    unsigned char* dst_bytes = (unsigned char*)dst;
    unsigned char encryption_key = read_protected_key();
    
    for (int i = 0; i < len; i++) {
        dst_bytes[i] = src_bytes[i] ^ encryption_key;
    }
}
