#!/bin/bash

# Сборка программы
make clean
make

if [ ! -f ./myprogram ]; then
    echo "Build failed!"
    exit 1
fi

# Файл для результатов
RESULT_FILE="result.txt"
> "$RESULT_FILE"

echo "=== Sparse file test report ===" >> "$RESULT_FILE"
echo "" >> "$RESULT_FILE"

# 1. Создание тестового файла A
echo "1. Creating test file A..." | tee -a "$RESULT_FILE"
./create_test_file.sh >> "$RESULT_FILE" 2>&1

# 2. Копирование A -> B через программу (разреженный)
echo "" >> "$RESULT_FILE"
echo "2. Copying A to B with sparse support (default block 4096)..." | tee -a "$RESULT_FILE"
./myprogram fileA fileB >> "$RESULT_FILE" 2>&1

# 3. Сжатие A и B с помощью gzip
echo "" >> "$RESULT_FILE"
echo "3. Compressing A and B with gzip..." | tee -a "$RESULT_FILE"
gzip -c fileA > fileA.gz
gzip -c fileB > fileB.gz

# 4. Распаковка B.gz и запись через программу в C
echo "" >> "$RESULT_FILE"
echo "4. Decompressing B.gz and writing to C via program..." | tee -a "$RESULT_FILE"
gzip -cd fileB.gz | ./myprogram fileC >> "$RESULT_FILE" 2>&1

# 5. Копирование A -> D с нестандартным блоком 100 байт
echo "" >> "$RESULT_FILE"
echo "5. Copying A to D with custom block size 100 bytes..." | tee -a "$RESULT_FILE"
./myprogram -b 100 fileA fileD >> "$RESULT_FILE" 2>&1

# 6. Статистика по файлам
echo "" >> "$RESULT_FILE"
echo "6. File statistics:" >> "$RESULT_FILE"
echo "-------------------" >> "$RESULT_FILE"

for file in fileA fileA.gz fileB fileB.gz fileC fileD; do
    if [ -f "$file" ]; then
        echo "$file:" >> "$RESULT_FILE"
        stat -c "  Size (real): %s bytes, Blocks: %b, Disk usage: %B blocks" "$file" >> "$RESULT_FILE" 2>&1
    else
        echo "$file: not found" >> "$RESULT_FILE"
    fi
done

echo "" >> "$RESULT_FILE"
echo "=== Test completed ===" >> "$RESULT_FILE"

# Вывод результата в консоль
cat "$RESULT_FILE"