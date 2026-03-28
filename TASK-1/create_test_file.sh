#!/bin/bash

OUTPUT="fileA"
SIZE=$((4*1024*1024 + 1))  # 4*1024*1024 + 1 байт

# Создаём файл с нулями
dd if=/dev/zero of="$OUTPUT" bs=1 count="$SIZE" 2>/dev/null

# Записываем единицы по смещениям
echo -n -e '\x01' | dd of="$OUTPUT" bs=1 seek=0 count=1 conv=notrunc 2>/dev/null
echo -n -e '\x01' | dd of="$OUTPUT" bs=1 seek=10000 count=1 conv=notrunc 2>/dev/null
echo -n -e '\x01' | dd of="$OUTPUT" bs=1 seek=$((SIZE-1)) count=1 conv=notrunc 2>/dev/null

echo "Test file $OUTPUT created (size: $SIZE bytes)"