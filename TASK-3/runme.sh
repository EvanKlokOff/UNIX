#!/bin/bash

# Очистка перед тестом
echo "Cleaning up previous runs..."
pkill -9 -x myinit 2>/dev/null || true
pkill -9 -x sleep 2>/dev/null || true
rm -f /tmp/myinit.log /tmp/myinit.pid
rm -f result.txt
rm -rf test
mkdir -p test

# Сохраняем абсолютный путь
CURRENT_DIR=$(pwd)

# Создание конфигов
cat > test/config1.txt << EOF
/bin/sleep 30 /dev/null ${CURRENT_DIR}/test/sleep1.out
/bin/sleep 30 /dev/null ${CURRENT_DIR}/test/sleep2.out
/bin/sleep 30 /dev/null ${CURRENT_DIR}/test/sleep3.out
EOF

cat > test/config2.txt << EOF
/bin/sleep 30 /dev/null ${CURRENT_DIR}/test/sleep1.out
EOF

# Компиляция
echo "Compiling myinit..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

if [ ! -f ./myinit ]; then
    echo "ERROR: Compilation failed"
    exit 1
fi

echo "Compilation successful"

# Запуск демона
echo "Starting myinit daemon..."
./myinit -c "${CURRENT_DIR}/test/config1.txt"

sleep 2

# Проверяем запустился ли
if ! pgrep -x myinit > /dev/null; then
    echo "ERROR: myinit failed to start"
    cat /tmp/myinit.log
    exit 1
fi

MYINIT_PID=$(pgrep -x myinit)
echo "myinit started with PID: $MYINIT_PID"

# Ждём запуска процессов
sleep 3

# Проверяем процессы
SLEEP_COUNT=$(pgrep -x sleep | wc -l)
echo "Sleep processes count: $SLEEP_COUNT"

if [ "$SLEEP_COUNT" -eq 3 ]; then
    echo "✓ Test 1 PASSED: 3 processes running"
    TEST1_RESULT="PASS"
else
    echo "✗ Test 1 FAILED: Expected 3, got $SLEEP_COUNT"
    TEST1_RESULT="FAIL"
fi

# Убиваем процесс номер 2 (индекс 1)
SLEEP_PIDS=($(pgrep -x sleep))
if [ ${#SLEEP_PIDS[@]} -ge 2 ]; then
    echo "Killing process 2 (PID: ${SLEEP_PIDS[1]})..."
    kill ${SLEEP_PIDS[1]}
    sleep 3  # Даём время на рестарт
    
    NEW_COUNT=$(pgrep -x sleep | wc -l)
    echo "Sleep processes after restart: $NEW_COUNT"
    
    if [ "$NEW_COUNT" -eq 3 ]; then
        echo "✓ Test 2 PASSED: Process was restarted"
        TEST2_RESULT="PASS"
    else
        echo "✗ Test 2 FAILED: Expected 3, got $NEW_COUNT"
        TEST2_RESULT="FAIL"
    fi
else
    echo "✗ Test 2 FAILED: Not enough sleep processes"
    TEST2_RESULT="FAIL"
fi

# SIGHUP тест
echo "Replacing config file with single process config..."
cat > test/config1.txt << EOF
/bin/sleep 30 /dev/null ${CURRENT_DIR}/test/sleep1.out
EOF

sleep 1

echo "Sending SIGHUP to myinit (PID: $MYINIT_PID)..."
kill -HUP $MYINIT_PID

# Даём время на перезагрузку
sleep 4

FINAL_COUNT=$(pgrep -x sleep | wc -l)
echo "Final sleep processes count: $FINAL_COUNT"

if [ "$FINAL_COUNT" -eq 1 ]; then
    echo "✓ Test 3 PASSED: Single process after SIGHUP"
    TEST3_RESULT="PASS"
else
    echo "✗ Test 3 FAILED: Expected 1, got $FINAL_COUNT"
    TEST3_RESULT="FAIL"
fi

# Останавливаем демона
echo "Stopping myinit daemon..."
kill $MYINIT_PID 2>/dev/null
sleep 2

# Принудительно убиваем, если остался
if pgrep -x myinit > /dev/null; then
    pkill -9 -x myinit 2>/dev/null
fi

# Сохраняем результат
{
    echo "=== Test Results ==="
    echo "Test 1 (3 processes): $TEST1_RESULT"
    echo "Test 2 (restart): $TEST2_RESULT"
    echo "Test 3 (SIGHUP): $TEST3_RESULT"
    echo ""
    echo "=== Log contents ==="
    cat /tmp/myinit.log
} > result.txt

echo ""
echo "========================================="
echo "Test completed. See result.txt for details"
echo "========================================="

if [ "$TEST1_RESULT" = "FAIL" ] || [ "$TEST2_RESULT" = "FAIL" ] || [ "$TEST3_RESULT" = "FAIL" ]; then
    exit 1
fi

exit 0