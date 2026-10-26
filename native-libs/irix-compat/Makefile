# Native x86-64 Linux build of IRIX userland commands against glibc plus a thin
# compat layer.  The IRIX sources are used unmodified from SRC.
SRC   ?= $(HOME)/Projects/irix-657m-src
OUT   ?= build
CC    ?= gcc
CFLAGS ?= -O2 -std=gnu89 -w -D_GNU_SOURCE -D_SGI_SOURCE -Iinclude -include include/irix_compat.h

SRCS_chkconfig = $(SRC)/eoe/cmd/chkconfig/chkconfig.c
SRCS_setmnt    = $(SRC)/eoe/cmd/setmnt/setmnt.c
SRCS_devnm     = src/devnm.c
PROGS = chkconfig setmnt devnm

all: $(addprefix $(OUT)/,$(PROGS))

$(OUT)/%: | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(SRCS_$*)

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)
.PHONY: all clean
