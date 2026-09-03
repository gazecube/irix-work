CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LDLIBS += -lX11 -lXext

all: iid-desktop

iid-desktop: iid-desktop.c sgi_icon.c sgi_icon.h
	$(CC) $(CFLAGS) -o $@ iid-desktop.c sgi_icon.c $(LDLIBS)

clean:
	rm -f iid-desktop

.PHONY: all clean
