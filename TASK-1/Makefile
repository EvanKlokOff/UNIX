CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = myprogram
SOURCES = myprogram.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET) fileA fileB fileC fileD *.gz result.txt

.PHONY: all clean