#!/bin/bash

# Файлы
PROGRAM="lock_program"
SOURCE="program.c"
LOCK_FILE="shared_file.lck"
TARGET_FILE="shared_file"

# Параметры теста
DURATION=300  # 5 минут
NUM_PROCESSES=10


# Запуск теста
run_test() {
    echo "Запуск $NUM_PROCESSES процессов на $DURATION секунд..."
    
    # Очищаем файл с сырыми данными
    > raw_stats.txt
    
    declare -a pids
    
    # Запускаем процессы
    for i in $(seq 1 $NUM_PROCESSES); do
        ./"$PROGRAM" -f "$TARGET_FILE" -s raw_stats.txt &
        pids+=($!)
        echo "Процесс $i: PID ${pids[-1]}"
    done
    
    echo "Ждем $DURATION секунд..."
    sleep $DURATION
    
    echo "Отправка SIGINT"
    for pid in "${pids[@]}"; do
        kill -INT "$pid" 2>/dev/null
    done
    
    sleep 2
    
    # Принудительное завершение зависших
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -KILL "$pid" 2>/dev/null
        fi
    done
    
    echo "Тест завершен"
}

# Сбор и анализ статистики (ВСЯ ЛОГИКА ЗДЕСЬ)
collect_and_analyze() {
    echo -e "\nСБОР СТАТИСТИКИ"
    
    if [ ! -f raw_stats.txt ]; then
        echo "Ошибка: файл статистики не найден!"
        return 1
    fi
    
    # Создаем финальный файл с привязкой к PID процессов
    echo "PID | Количество блокировок" > final_stats.txt
    echo "------------------------" >> final_stats.txt
    
    # Нумеруем строки (каждая строка - результат одного процесса)
    line_num=1
    total_locks=0
    declare -a locks_array
    
    while read locks; do
        if [ -n "$locks" ]; then
            echo "Процесс $line_num | $locks" >> final_stats.txt
            total_locks=$((total_locks + locks))
            locks_array+=($locks)
            line_num=$((line_num + 1))
        fi
    done < raw_stats.txt
    
    actual_processes=$((line_num - 1))
    
    echo -e "\nРЕЗУЛЬТАТЫ"
    echo "Ожидалось процессов: $NUM_PROCESSES"
    echo "Получено результатов: $actual_processes"
    
    if [ $actual_processes -eq $NUM_PROCESSES ]; then
        echo "Все процессы успешно записали статистику"
    else
        echo "Потеряно $((NUM_PROCESSES - actual_processes)) процессов"
    fi
    
    echo -e "\n СТАТИСТИКА БЛОКИРОВОК"
    cat final_stats.txt
    
    # Анализ распределения
    if [ $actual_processes -gt 0 ]; then
        average=$((total_locks / actual_processes))
        echo -e "\nСреднее блокировок на процесс: $average"
        echo "Всего блокировок: $total_locks"
        
        # Проверка равномерности (отклонение не более 30%)
        uniform=true
        max_diff=0
        
        for locks in "${locks_array[@]}"; do
            diff=$((locks - average))
            if [ ${diff#-} -gt $max_diff ]; then
                max_diff=${diff#-}
            fi
            
            if [ ${diff#-} -gt $((average * 30 / 100)) ]; then
                uniform=false
            fi
        done
        
        if [ "$uniform" = true ]; then
            echo "Распределение равномерное (макс. отклонение: $max_diff)"
            echo "Нет голодания процессов"
        else
            echo "Распределение может быть неравномерным (макс. отклонение: $max_diff)"
        fi
    fi
    
    # Проверка файла блокировки
    if [ -f "$LOCK_FILE" ]; then
        echo "Файл блокировки все еще существует!"
    else
        echo "Файл блокировки удален"
    fi
}

# Сохранение отчета
save_report() {
    {
        echo "ОТЧЕТ О ТЕСТЕ"
        echo "Длительность: $DURATION секунд"
        echo "Количество процессов: $NUM_PROCESSES"
        echo ""
        cat final_stats.txt
        if [ -f final_stats.txt ] && [ $(wc -l < final_stats.txt) -eq $((NUM_PROCESSES + 2)) ]; then
            echo "Все $NUM_PROCESSES процессов завершились корректно"
        else
            echo "Обнаружены проблемы при выполнении"
        fi
        echo ""
    } > result.txt
    
    echo -e "\nОтчет сохранен в result.txt"
}
# Основная функция
main() {
    run_test
    collect_and_analyze
    save_report    
}
main