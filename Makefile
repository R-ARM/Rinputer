PKG_CONFIG ?= pkg-config

all:
	$(CC) main.c -o rinputer -O3 -lpthread -Wall -Wextra -Wpedantic $(CFLAGS)
