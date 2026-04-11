CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = lock_program
SOURCE = program.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET) *.lck raw_stats.txt final_stats.txt result.txt

.PHONY: all clean