#include "caesar.h"
#include <string.h>

static char encryption_key = 0;

/**
 * Устанавливает ключ шифрования.
 * @param key Ключ шифрования (один байт)
 */
void set_key(char key) {
    encryption_key = key;
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
    
    for (int i = 0; i < len; i++) {
        dst_bytes[i] = src_bytes[i] ^ encryption_key;
    }
}
