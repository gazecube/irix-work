CC ?= cc
CPPFLAGS += -Iinclude
CFLAGS += -std=gnu11 -O2 -g -Wall -Wextra -fPIC
all: build/libirixdev.so build/libirixdev.a
build/irixdev.o: src/irixdev.c include/irixdev.h include/invent_compat.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/libirixdev.so: build/irixdev.o
	$(CC) -shared -Wl,-soname,libirixdev.so $^ -o $@
build/libirixdev.a: build/irixdev.o
	$(AR) rcs $@ $^
check: all
	python3 -m unittest discover -s tests -v
.PHONY: all check
