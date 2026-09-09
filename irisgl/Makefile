CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -g -Wall -Wextra -Werror
CPPFLAGS += -Iinclude -Iinclude/gl -Isrc

LIB = libgl.a
OBJS = src/igl_runtime.o src/igl_backend_x11.o
X11_LIBS = -lX11 -lXpresent

LEGACY_CFLAGS = -std=gnu89 \
	-Wno-old-style-definition -Wno-strict-prototypes \
	-Wno-implicit-int -Wno-implicit-function-declaration \
	-Wno-return-type -Wno-builtin-declaration-mismatch \
	-Wno-unused-parameter -Wno-parentheses \
	-D_MIPS_SIM=1 -D_MIPS_SIM_NABI32=2

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

src/igl_runtime.o: src/igl_runtime.c src/igl_state.h src/igl_backend.h include/gl/gl.h include/gl/device.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/igl_backend_x11.o: src/igl_backend_x11.c src/igl_backend.h include/gl/device.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

flash: $(LIB)
	@test -n "$(SGI_FLASH)" || (echo "Set SGI_FLASH=/path/to/original/irix/cmd/stress/Gfx/flash.c"; exit 1)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LEGACY_CFLAGS) \
		-o $@ $(SGI_FLASH) -L. -lgl $(X11_LIBS) -lm

mode: $(LIB)
	@test -n "$(SGI_MODE)" || (echo "Set SGI_MODE=/path/to/original/irix/cmd/stress/Gfx/mode.c"; exit 1)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LEGACY_CFLAGS) \
		-o $@ $(SGI_MODE) -L. -lgl $(X11_LIBS) -lm

badifred: $(LIB)
	@test -n "$(SGI_BADIFRED)" || (echo "Set SGI_BADIFRED=/path/to/original/irix/cmd/stress/Gfx/badifred.c"; exit 1)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LEGACY_CFLAGS) \
		-o $@ $(SGI_BADIFRED) -L. -lgl $(X11_LIBS) -lm

rbuf: $(LIB)
	@test -n "$(SGI_RBUF)" || (echo "Set SGI_RBUF=/path/to/original/irix/cmd/stress/Gfx/rbuf.c"; exit 1)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LEGACY_CFLAGS) \
		-o $@ $(SGI_RBUF) -L. -lgl $(X11_LIBS) -lm

cbuf: $(LIB)
	@test -n "$(SGI_CBUF)" || (echo "Set SGI_CBUF=/path/to/original/irix/cmd/stress/Gfx/cbuf.c"; exit 1)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LEGACY_CFLAGS) \
		-o $@ $(SGI_CBUF) -L. -lgl $(X11_LIBS) -lm

gsync: $(LIB)
	@test -n "$(SGI_GSYNC)" || (echo "Set SGI_GSYNC=/path/to/original/irix/cmd/stress/Gfx/gsync.c"; exit 1)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LEGACY_CFLAGS) \
		-o $@ $(SGI_GSYNC) -L. -lgl $(X11_LIBS) -lm

clean:
	rm -f $(OBJS) $(LIB) flash mode badifred rbuf cbuf gsync

.PHONY: all clean flash mode badifred rbuf cbuf gsync
