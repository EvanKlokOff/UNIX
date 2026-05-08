CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_GNU_SOURCE
TARGET = myinit
SOURCES = program.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET) $(TARGET).o

distclean: clean
	rm -f /tmp/myinit.log /tmp/myinit.pid result.txt
	rm -rf test

.PHONY: all clean distclean