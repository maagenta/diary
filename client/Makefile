CC      = gcc
CC_ARM  = aarch64-linux-gnu-gcc
CFLAGS  = -Wall -Wextra -O2 -I..
LDFLAGS = -lsodium -lncurses
BUILD  ?= ../build

TARGET      = $(BUILD)/diary-client
TARGET_ARM  = $(BUILD)/diary-client-arm64
SRCS        = main.c net.c crypto.c ui.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

arm64: $(SRCS)
	$(CC_ARM) $(CFLAGS) -o $(TARGET_ARM) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TARGET_ARM)

.PHONY: clean arm64
