CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lsodium
BUILD  ?= ../build

TARGET  = $(BUILD)/keygen

$(TARGET): keygen.c
	$(CC) $(CFLAGS) -o $@ keygen.c $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
