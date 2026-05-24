#!/bin/bash

# Configuration
SERVER_LOG="/tmp/brownian_bot.log"
TEST_DATA_FILE="test_data.txt"
RESULT_FILE="result.txt"
CLIENT_LOGS_DIR="client_logs"
NUM_CLIENTS=100
NUM_NUMBERS=1000
MAX_DELAY_MS=255
DELAY_STEPS="0 200 400 600 800 1000"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_status() { echo -e "${GREEN}[INFO]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARN]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

cleanup() {
    print_status "Cleaning up..."
    pkill -f "./server" 2>/dev/null
    pkill -f "./client" 2>/dev/null
    rm -f /tmp/brownian_bot_socket
    rm -f /tmp/bsocket_config
    rm -rf $CLIENT_LOGS_DIR
    rm -f server client
    rm -f /tmp/final_state_check.log
    sleep 1
}

create_test_data() {
    print_status "Creating test data file with $NUM_NUMBERS numbers (sum = 0)..."
    
    > $TEST_DATA_FILE
    local sum=0
    
    for i in $(seq 1 $((NUM_NUMBERS - 1))); do
        local num=$((RANDOM % 101 - 50))
        echo $num >> $TEST_DATA_FILE
        sum=$((sum + num))
    done
    
    local last_num=$((-sum))
    echo $last_num >> $TEST_DATA_FILE
    
    # Удаляем возможный большой хвост (из предыдущего запуска)
    sed -i '/^-*[0-9]\{4,\}$/d' $TEST_DATA_FILE 2>/dev/null
    
    local actual_sum=$(awk '{sum+=$1} END {print sum}' $TEST_DATA_FILE)
    print_status "Test data created. Sum verification: $actual_sum (expected 0)"
}

build_programs() {
    print_status "Building programs..."
    make clean
    make
    
    if [ ! -f "./server" ] || [ ! -f "./client" ]; then
        print_error "Build failed!"
        exit 1
    fi
    
    print_status "Build successful"
}

setup_config() {
    print_status "Setting up configuration..."
    echo "/tmp/brownian_bot_socket" > /tmp/bsocket_config
}

start_server() {
    print_status "Starting server..."
    rm -f $SERVER_LOG
    ./server &
    SERVER_PID=$!
    sleep 2
    
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        print_error "Server failed to start"
        exit 1
    fi
    
    print_status "Server started with PID: $SERVER_PID"
}

stop_server() {
    print_status "Stopping server..."
    kill $SERVER_PID 2>/dev/null
    sleep 2
    pkill -f "./server" 2>/dev/null
}

# Функция для проверки состояния сервера
check_server_state() {
    local response=$(timeout 5 ./client <<< "0" 2>/dev/null | grep "Response:" | tail -1 | awk '{print $2}')
    echo "${response:-ERROR}"
}

run_test() {
    local num_clients=$1
    local delay_ms=$2
    local test_id=$3
    
    print_status "Running test $test_id: $num_clients clients, delay ${delay_ms}ms"
    
    local test_log_dir="$CLIENT_LOGS_DIR/test_${test_id}"
    mkdir -p $test_log_dir
    
    local start_time=$(date +%s.%N)
    
    local client_pids=()
    for i in $(seq 1 $num_clients); do
        local client_log="$test_log_dir/client_${i}.log"
        ./client -f $TEST_DATA_FILE -d $delay_ms -l $client_log &
        client_pids+=($!)
    done
    
    # Ждем завершения всех клиентов
    for pid in "${client_pids[@]}"; do
        wait $pid 2>/dev/null
    done
    
    # Даем серверу время обработать все оставшиеся данные
    sleep 2
    
    local end_time=$(date +%s.%N)
    
    # Проверяем финальное состояние
    local final_state=$(check_server_state)
    
    local total_time=$(echo "$end_time - $start_time" | bc)
    
    local max_delay=0
    local total_numbers=0
    local all_zero_check=true
    local failed_clients=0
    
    for log_file in $test_log_dir/client_*.log; do
        if [ -f "$log_file" ]; then
            local delay=$(grep "Total delay:" $log_file | awk '{print $3}')
            local numbers=$(grep "Numbers processed:" $log_file | awk '{print $3}')
            local final_response=$(grep "Final server response:" $log_file | sed 's/.*Final server response: //' | tr -d '\n' | xargs)
            
            total_numbers=$((total_numbers + numbers))
            
            if [ -n "$final_response" ] && [ "$final_response" != "0" ]; then
                all_zero_check=false
                failed_clients=$((failed_clients + 1))
            fi
            
            if [ -n "$delay" ] && (( $(echo "$delay > $max_delay" | bc -l) )); then
                max_delay=$delay
            fi
        fi
    done
    
    local effective_time=$(echo "$total_time - $max_delay" | bc)
    
    echo "Test $test_id: Clients=$num_clients, Delay=${delay_ms}ms" >> $RESULT_FILE
    echo "  Final server state: $final_state" >> $RESULT_FILE
    echo "  Total numbers processed: $total_numbers (expected: $((num_clients * NUM_NUMBERS)))" >> $RESULT_FILE
    echo "  Failed clients (zero check): $failed_clients" >> $RESULT_FILE
    echo "  Total time: ${total_time}s" >> $RESULT_FILE
    echo "  Max client delay: ${max_delay}s" >> $RESULT_FILE
    echo "  Effective time: ${effective_time}s" >> $RESULT_FILE
    
    # Успех если сервер вернул 0 (клиенты могут ошибаться из-за гонок)
    if [ "$final_state" = "0" ]; then
        if [ "$all_zero_check" = true ]; then
            echo "  Result: PASSED" >> $RESULT_FILE
            print_status "Test $test_id PASSED"
        else
            echo "  Result: PASSED (state OK, $failed_clients clients had race conditions)" >> $RESULT_FILE
            print_status "Test $test_id PASSED (with minor client races)"
        fi
        return 0
    else
        echo "  Result: FAILED (server state: $final_state)" >> $RESULT_FILE
        print_error "Test $test_id FAILED - server state: $final_state"
        return 1
    fi
}

check_resources() {
    print_status "Checking memory and file descriptor usage..."
    
    local first_heap=$(grep "Heap:" $SERVER_LOG 2>/dev/null | head -1 | sed -n 's/.*Heap: \(0x[0-9a-f]*\).*/\1/p')
    local last_heap=$(grep "Heap:" $SERVER_LOG 2>/dev/null | tail -1 | sed -n 's/.*Heap: \(0x[0-9a-f]*\).*/\1/p')
    
    local first_fd=$(grep "Client connected on fd" $SERVER_LOG 2>/dev/null | head -1 | sed -n 's/.*fd \([0-9]*\).*/\1/p')
    local last_fd=$(grep "Client connected on fd" $SERVER_LOG 2>/dev/null | tail -1 | sed -n 's/.*fd \([0-9]*\).*/\1/p')
    
    echo "Resource Usage Analysis:" >> $RESULT_FILE
    echo "  First connection: fd=$first_fd, heap=$first_heap" >> $RESULT_FILE
    echo "  Last connection: fd=$last_fd, heap=$last_heap" >> $RESULT_FILE
    echo "" >> $RESULT_FILE
}

run_performance_experiments() {
    print_status "Running performance experiments..."
    
    echo "=== Performance Analysis ===" >> $RESULT_FILE
    echo "" >> $RESULT_FILE
    
    local test_id=100
    for num_clients in 1 5 10 50 100; do
        for delay_ms in 0 200 400 600 800 1000; do
            # stop_server
            > $SERVER_LOG
            # start_server
            
            run_test $num_clients $delay_ms $test_id
            test_id=$((test_id + 1))
            
            sleep 2
        done
    done
}

main() {
    print_status "Starting Brownian Bot test suite..."
    
    cleanup
    > $RESULT_FILE
    build_programs    
    mkdir -p $CLIENT_LOGS_DIR
    setup_config
    create_test_data
    
    start_server
    
    # Test 1: Basic functionality
    print_status "=== Test 1: Basic Functionality ==="
    echo "=== Test 1: Basic Functionality Verification ===" >> $RESULT_FILE
    echo "" >> $RESULT_FILE
    
    print_status "Running verification test with single client..."
    local single_test_log="$CLIENT_LOGS_DIR/verification_test.log"
    
    # Запускаем тест и сохраняем вывод
    ./client -f $TEST_DATA_FILE -d 0 -l "$single_test_log" > /tmp/verification_output.txt 2>&1
    
    sleep 1
    
    # Извлекаем финальный ответ из вывода программы (строка FINAL_RESPONSE=...)
    local zero_check=$(grep "FINAL_RESPONSE=" /tmp/verification_output.txt | cut -d'=' -f2 | tr -d '\n')
    
    # Если не нашли, пробуем из лога
    if [ -z "$zero_check" ] && [ -f "$single_test_log" ]; then
        zero_check=$(grep "Final server response:" "$single_test_log" | sed 's/.*Final server response: //' | tr -d '\n' | xargs)
    fi
    
    # Если все еще пусто, пробуем последнюю строку с числом
    if [ -z "$zero_check" ] && [ -f "$single_test_log" ]; then
        zero_check=$(tail -5 "$single_test_log" | grep -E '^[0-9]+$' | tail -1)
    fi
    
    echo "Verification Test:" >> $RESULT_FILE
    echo "  Single client processed all numbers from test file" >> $RESULT_FILE
    echo "  Final server response to '0' query: $zero_check" >> $RESULT_FILE
    
    if [ "$zero_check" = "0" ]; then
        echo "  Result: PASSED" >> $RESULT_FILE
        print_status "Verification test PASSED"
    else
        echo "  Result: FAILED (response: '$zero_check')" >> $RESULT_FILE
        print_error "Verification test FAILED (response: '$zero_check')"
        # Отладочный вывод
        print_warning "Client output:"
        cat /tmp/verification_output.txt | head -5
    fi
    echo "" >> $RESULT_FILE
    
    # Test 2: Multiple clients
    print_status "=== Test 2: 100 parallel clients ==="
    echo "=== Test 2: 100 parallel clients ===" >> $RESULT_FILE
    echo "" >> $RESULT_FILE
    
    run_test 100 200 "test2_small"
    check_resources
    
    # Test 3: Performance experiments
    print_status "=== Test 3: Performance Experiments ==="
    run_performance_experiments
    
    stop_server
    cleanup
    
    print_status "All tests completed. Results saved to $RESULT_FILE"
    
    echo ""
    print_status "Summary of results:"
    echo "======================"
    grep "Result:" $RESULT_FILE
}

main