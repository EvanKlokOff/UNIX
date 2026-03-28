#!/bin/bash

PROG="./sparse_data"

make clean
make

if [ ! -f "$PROG" ]; then
    echo "Build failed. Error: $PROG not found."
    exit 1
fi

RESULT_FILE="result.txt"
> "$RESULT_FILE"

echo "Sparse file Report" >> "$RESULT_FILE"
echo "" >> "$RESULT_FILE"

echo "1. Creating test file A" | tee -a "$RESULT_FILE"
./create_test_file.sh >> "$RESULT_FILE" 2>&1

echo "2. Copying A to B (sparse)" | tee -a "$RESULT_FILE"
$PROG fileA fileB >> "$RESULT_FILE" 2>&1

echo "3. Compressing A and B with gzip" | tee -a "$RESULT_FILE"
gzip -c fileA > fileA.gz
gzip -c fileB > fileB.gz

echo "4. Decompressing B.gz to stdout and saving to C via program" | tee -a "$RESULT_FILE"
gzip -cd fileB.gz | $PROG fileC >> "$RESULT_FILE" 2>&1

echo "5. Copying A to D with custom block size 100 bytes" | tee -a "$RESULT_FILE"
$PROG -b 100 fileA fileD >> "$RESULT_FILE" 2>&1

echo "" >> "$RESULT_FILE"
echo "6. File stats (Logical size vs Disk usage):" >> "$RESULT_FILE"
echo "###############################################" >> "$RESULT_FILE"

FILES=("fileA" "fileA.gz" "fileB" "fileB.gz" "fileC" "fileD")

for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "File: $file" >> "$RESULT_FILE"
        stat -c "Logical size: %s bytes | Disk blocks: %b (x512B)" "$file" >> "$RESULT_FILE"
    else
        echo "File: $file - NOT FOUND" >> "$RESULT_FILE"
    fi
done

cat "$RESULT_FILE"