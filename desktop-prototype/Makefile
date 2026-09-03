CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LDLIBS += -lX11

all: iid-desktop

iid-desktop: iid-desktop.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f iid-desktop

.PHONY: all clean
