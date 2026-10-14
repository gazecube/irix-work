SRC ?= $(HOME)/Projects/irix-657m-src/eoe/cmd/gr_osview
REPO := $(abspath ../../..)
CC = clang
CFLAGS = -m32 -O2 -w -Wno-error=incompatible-function-pointer-types -D_GNU_SOURCE -DFIXGFX -D_KMEMUSER -D_LANGUAGE_C \
         -Iinclude -include irixcompat.h -I$(SRC)
CFILES = main setup initbar drawbar input getinfo rtbar remote
OBJ = $(addprefix build/,$(addsuffix .o,$(CFILES))) build/shim.o build/kinfo.o build/iglxlib.o

all: gr_osview

build:
	mkdir -p build

build/%.o: $(SRC)/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/drawbar.o: CFLAGS += -Dargs=ap

build/shim.o: shim.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/kinfo.o: $(REPO)/irix-re/recomp/runtime/IrixKernelInfo.c | build
	$(CC) -m32 -O2 -w -D_GNU_SOURCE -Iinclude -c $< -o $@

build/iglxlib.o: $(REPO)/desktop-integration/n32-toolchain/irisgl/iglxlib.c | build
	$(CC) -m32 -O2 -w -D_GNU_SOURCE -Iinclude -c $< -o $@

gr_osview: $(OBJ)
	$(CC) -m32 -o $@ $(OBJ) -lX11 -lm

clean:
	rm -rf build gr_osview
