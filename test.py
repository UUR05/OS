#!/usr/bin/env python3
"""
Тестовая программа для libcaesar.
Использование: python3 test.py <путь_к_библиотеке> <ключ> <входной_файл> <выходной_файл>
"""

import sys
import ctypes
from ctypes import c_char, c_void_p, c_int
import os

def load_library(lib_path):
    try:
        lib = ctypes.CDLL(lib_path)
        return lib
    except OSError as e:
        print(f"Ошибка загрузки библиотеки: {e}")
        sys.exit(1)

def setup_functions(lib):
    lib.set_key.argtypes = [c_char]
    lib.set_key.restype = None
    lib.caesar.argtypes = [c_void_p, c_void_p, c_int]
    lib.caesar.restype = None
    return lib.set_key, lib.caesar

def read_file(file_path):
    try:
        with open(file_path, 'rb') as f:
            data = f.read()
        return data
    except FileNotFoundError:
        print(f"Файл не найден: {file_path}")
        sys.exit(1)
    except IOError as e:
        print(f"Ошибка чтения файла: {e}")
        sys.exit(1)

def write_file(file_path, data):
    try:
        with open(file_path, 'wb') as f:
            f.write(data)
    except IOError as e:
        print(f"Ошибка записи файла: {e}")
        sys.exit(1)

def encrypt_decrypt(set_key_func, caesar_func, data, key):
    key_byte = c_char(bytes([key & 0xFF]))
    set_key_func(key_byte)
    dst_buffer = ctypes.create_string_buffer(len(data))
    src_buffer = ctypes.create_string_buffer(data)
    caesar_func(src_buffer, dst_buffer, len(data))
    return dst_buffer.raw

def verify_symmetry(set_key_func, caesar_func, original_data, key):
    encrypted = encrypt_decrypt(set_key_func, caesar_func, original_data, key)
    decrypted = encrypt_decrypt(set_key_func, caesar_func, encrypted, key)
    return original_data == decrypted

def print_hex_sample(data, label, max_bytes=16):
    sample = data[:max_bytes]
    hex_str = ' '.join(f'{b:02x}' for b in sample)
    if len(data) > max_bytes:
        hex_str += '...'
    print(f"  {label}: {hex_str}")

def main():
    """Основное выполнение тестовой программы."""
    
    # Разобрать аргументы командной строки
    if len(sys.argv) != 5:
        print("Использование: python3 test.py <путь_к_библиотеке> <ключ> <входной_файл> <выходной_файл>")
        print("")
        print("Аргументы:")
        print("  путь_к_библиотеке : Путь к libcaesar.so (например, ./libcaesar.so)")
        print("  ключ              : Ключ шифрования (0-255)")
        print("  входной_файл      : Путь к файлу для шифрования")
        print("  выходной_файл     : Путь к файлу для зашифрованных данных")
        print("")
        print("Пример: python3 test.py ./libcaesar.so 42 input.txt output.bin")
        sys.exit(1)
    
    lib_path = sys.argv[1]
    key = int(sys.argv[2])
    input_file = sys.argv[3]
    output_file = sys.argv[4]
    
    # Проверить диапазон ключа
    if key < 0 or key > 255:
        print(f"✗ Ключ должен быть в диапазоне 0-255, получено: {key}")
        sys.exit(1)
    
    print("=" * 60)
    print("Тестовая программа шифра XOR libcaesar")
    print("=" * 60)
    print("")
    
    lib = load_library(lib_path)
    set_key_func, caesar_func = setup_functions(lib)
    
    original_data = read_file(input_file)
    print(f"Прочитано {len(original_data)} байт из: {input_file}")
    print_hex_sample(original_data, "Входные данные")
    
    result_data = encrypt_decrypt(set_key_func, caesar_func, original_data, key)
    print_hex_sample(result_data, "Выходные данные")
    
    write_file(output_file, result_data)
    print(f"Записано {len(result_data)} байт в: {output_file}")
    
    if verify_symmetry(set_key_func, caesar_func, original_data, key):
        print("Симметрия XOR проверена")
    else:
        print("Ошибка симметрии")
    
    print("=" * 60)
    print("Тест завершён")
    print("=" * 60)

if __name__ == "__main__":
    main()
