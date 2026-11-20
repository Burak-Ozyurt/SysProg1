CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -D_XOPEN_SOURCE=700 -Iinclude
SRC     = $(wildcard src/*.c)
HDR     = $(wildcard include/*.h)

.PHONY: all clean

all: watchdog worker

watchdog: $(SRC) $(HDR) ; $(CC) $(CFLAGS) -o $@ $(SRC)

worker: workers/worker.c ; $(CC) $(CFLAGS) -o $@ $<

clean: ; rm -f watchdog worker
