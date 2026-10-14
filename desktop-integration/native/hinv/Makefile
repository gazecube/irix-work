CC = clang
CFLAGS = -O2 -Wall
all: hinv
hinv: hinv.c
	$(CC) $(CFLAGS) -o $@ $<
clean:
	rm -f hinv

ROOT ?= $(abspath ../../../build-irix-re/recomp/root)
install: hinv
	install -D -m 755 hinv $(ROOT)/sbin/hinv
