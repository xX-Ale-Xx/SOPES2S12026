CC := gcc
CFLAGS := -O2 -Wall -Wextra -std=c11
LDLIBS := -lwebsockets -lpam -lpam_misc -lssl -lcrypto -pthread

all: daemon

daemon: daemon.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -f daemon

.PHONY: all clean
