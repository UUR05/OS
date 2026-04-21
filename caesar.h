#ifndef CAESAR_H
#define CAESAR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Устанавливает ключ шифрования.
 * @param key Ключ шифрования (один байт)
 */
void set_key(char key);

/**
 * Выполняет XOR-шифрование/дешифрование.
 * @param src Указатель на исходный буфер
 * @param dst Указатель на буфер назначения
 * @param len Количество байт для обработки
 */
void caesar(void* src, void* dst, int len);

#ifdef __cplusplus
}
#endif

#endif /* CAESAR_H */
