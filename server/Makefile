CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I..
LDFLAGS = -lsodium -lsqlite3
BUILD  ?= ../build

TARGET  = $(BUILD)/diary-server
SRCS    = main.c client_handler.c storage.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
