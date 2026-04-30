CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I..
LDFLAGS = -lsodium -lncurses
BUILD  ?= ../build

TARGET  = $(BUILD)/diary-client
SRCS    = main.c net.c crypto.c ui.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
