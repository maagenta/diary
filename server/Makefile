CC      = gcc
CC_ARM  = aarch64-linux-gnu-gcc
CFLAGS  = -Wall -Wextra -O2 -I..
LDFLAGS = -lsodium -lsqlite3
BUILD  ?= ../build

TARGET      = $(BUILD)/diary-server
TARGET_ARM  = $(BUILD)/diary-server-arm64
SRCS        = main.c client_handler.c storage.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

arm64: $(SRCS)
	$(CC_ARM) $(CFLAGS) -o $(TARGET_ARM) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TARGET_ARM)

.PHONY: clean arm64
