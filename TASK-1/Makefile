CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = sparse_data
SOURCES = program.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET) fileA fileB fileC fileD *.gz result.txt

.PHONY: all clean