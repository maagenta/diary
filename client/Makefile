CC      = gcc
CC_ARM  = aarch64-linux-gnu-gcc
CFLAGS  = -Wall -Wextra -O2 -I..

# UTF-8 needs wide-char ncurses; on macOS the system lib already includes it
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
NCURSES_LIB = -lncurses
else
NCURSES_LIB = -lncursesw
endif
LDFLAGS = -lsodium $(NCURSES_LIB)
BUILD  ?= ../build

TARGET      = $(BUILD)/diary-client
TARGET_ARM  = $(BUILD)/diary-client-arm64
SRCS        = main.c net.c crypto.c ui.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

arm64: $(SRCS)
	$(CC_ARM) $(CFLAGS) -o $(TARGET_ARM) $(SRCS) -lsodium -lncursesw

clean:
	rm -f $(TARGET) $(TARGET_ARM)

.PHONY: clean arm64
