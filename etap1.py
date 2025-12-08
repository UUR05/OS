import argparse
import sys
import re
MNEMONICS = {
    'LOAD_CONST': 7,
    'READ_MEM': 61,
    'WRITE_MEM': 27,
    'BITREVERSE': 25,
}
def parse_line(line):
    line = line.strip()
    if not line or line.startswith(';'):
        return None
    parts = re.split(r'\s*,\s*|\s+', line)
    mnemonic = parts[0].upper()
    args = parts[1:] if len(parts) > 1 else []
    if mnemonic not in MNEMONICS:
        raise ValueError(f"Неизвестная мнемоника: {mnemonic}")
    opcode = MNEMONICS[mnemonic]
    if mnemonic == 'LOAD_CONST':
        if len(args) != 1:
            raise ValueError("LOAD_CONST требует значение B (константа)")
        try:
            b_value = int(args[0], 0)
        except ValueError:
            raise ValueError("Значение B должно быть числом")
        return {'opcode': opcode, 'b_value': b_value, 'size': 5}
    elif mnemonic == 'READ_MEM':
        if len(args) != 1:
            raise ValueError("READ_MEM требует значение B (смещение)")
        try:
            b_value = int(args[0], 0)
        except ValueError:
            raise ValueError("Значение B должно быть числом")
        return {'opcode': opcode, 'b_value': b_value, 'size': 2}
    elif mnemonic == 'WRITE_MEM':
        if args:
            raise ValueError("WRITE_MEM не требует аргументов")
        return {'opcode': opcode, 'size': 1}
    elif mnemonic == 'BITREVERSE':
        if len(args) != 1:
            raise ValueError("BITREVERSE требует значение B?")
        try:
            b_value = int(args[0], 0)
        except ValueError:
            raise ValueError("Значение B должно быть числом")
        return {'opcode': opcode, 'b_value': b_value, 'size': 2}
    return None
def encode_to_bytes(instr):
    opcode = instr['opcode']
    size = instr['size']
    bytes_list = [0] * size
    if size == 1:
        bytes_list[0] = opcode  
    elif size == 2 or size == 5:
        if 'b_value' in instr:
            b_value = instr['b_value']
            value = opcode | (b_value << 6)
            for i in range(size):
                bytes_list[i] = (value >> (i * 8)) & 0xFF
    return bytes_list
def ir_to_string(ir):
    output = []
    for instr in ir:
        bytes_list = encode_to_bytes(instr)
        hex_str = ' '.join(hex(b) for b in bytes_list)
        output.append(hex_str)
    return '\n'.join(output)
def main():
    parser = argparse.ArgumentParser(description="Ассемблер для УВМ (Вариант 22)")
    parser.add_argument('input_file', type=str, help="Путь к входному файлу .asm")
    parser.add_argument('-o', '--output', type=str, help="Путь к выходному файлу IR", default=None)
    parser.add_argument('--test', action='store_true', help="Режим тестирования: вывести IR на экран в hex")
    args = parser.parse_args()
    try:
        with open(args.input_file, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        ir = []
        for line in lines:
            parsed = parse_line(line)
            if parsed:
                ir.append(parsed)
        if args.test:
            print("Промежуточное представление (hex-байты команд):")
            print(ir_to_string(ir))
        if args.output:
            with open(args.output, 'w', encoding='utf-8') as f:
                for instr in ir:
                    f.write(str(instr) + '\n')
    except Exception as e:
        print(f"Ошибка: {e}", file=sys.stderr)
        sys.exit(1)
if __name__ == "__main__":
    main()