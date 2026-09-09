CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -g -Wall -Wextra -Werror
CPPFLAGS += -Iinclude -Isrc

LIB = libgl.a
OBJS = src/igl_runtime.o src/igl_backend_x11.o

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

src/igl_runtime.o: src/igl_runtime.c src/igl_state.h src/igl_backend.h include/gl/gl.h include/gl/device.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/igl_backend_x11.o: src/igl_backend_x11.c src/igl_backend.h include/gl/device.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

flash: $(LIB)
	@test -n "$(SGI_FLASH)" || (echo "Set SGI_FLASH=/path/to/original/irix/cmd/stress/Gfx/flash.c"; exit 1)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-implicit-int -Wno-implicit-function-declaration \
		-o $@ $(SGI_FLASH) -L. -lgl -lX11 -lm

clean:
	rm -f $(OBJS) $(LIB) flash

.PHONY: all clean
