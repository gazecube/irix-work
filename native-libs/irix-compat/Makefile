# Native x86-64 Linux build of IRIX userland commands against glibc plus a thin
# compat layer.  The IRIX sources are used unmodified from SRC.
SRC   ?= $(HOME)/Projects/irix-657m-src
OUT   ?= build
CC    ?= gcc
CFLAGS ?= -O2 -std=gnu89 -w -D_GNU_SOURCE -D_SGI_SOURCE -Iinclude -include include/irix_compat.h

SRCS_chkconfig = $(SRC)/eoe/cmd/chkconfig/chkconfig.c
SRCS_setmnt    = $(SRC)/eoe/cmd/setmnt/setmnt.c
SRCS_devnm     = src/devnm.c
SRCS_uadmin    = $(SRC)/eoe/cmd/uadmin/uadmin.c
SRCS_suattr    = $(SRC)/eoe/cmd/suattr/suattr.c
SRCS_nvram     = $(SRC)/eoe/cmd/nvram/nvram.c
SRCS_getty     = $(SRC)/eoe/cmd/getty/getty.c
CFLAGS_getty   = -I$(SRC)/eoe/cmd/getty
SRCS_init      = $(OUT)/init.c
CFLAGS_init    = -I$(SRC)/eoe/cmd/init
PROGS = chkconfig setmnt devnm uadmin suattr nvram getty init

# init.c needs two source transforms, applied to a copy in $(OUT):
#  - SVR4 FILE internals (fp->_flag) -> irix_fflag(fp)
#  - the console device numbers: IRIX duart 58,0 -> Linux /dev/console 5,1
$(OUT)/init.c: $(SRC)/eoe/cmd/init/init.c | $(OUT)
	sed -e 's/fp->_flag/irix_fflag(fp)/g' \
	    -e 's/^#define\tREAL_CONSDEVMAJNUM\t(58)/#define\tREAL_CONSDEVMAJNUM\t(5)/' \
	    -e 's/^#define\tREAL_CONSDEVMINNUM\t(0)/#define\tREAL_CONSDEVMINNUM\t(1)/' $< > $@
	grep -q 'REAL_CONSDEVMAJNUM	(5)' $@ && grep -q 'REAL_CONSDEVMINNUM	(1)' $@

all: $(addprefix $(OUT)/,$(PROGS))

$(OUT)/libirixcompat.a: src/irix_compat.c | $(OUT)
	$(CC) -O2 -Wall -Iinclude -c -o $(OUT)/irix_compat.o src/irix_compat.c
	ar rcs $@ $(OUT)/irix_compat.o

$(OUT)/%: $(OUT)/libirixcompat.a | $(OUT)
	$(CC) $(CFLAGS) $(CFLAGS_$*) -o $@ $(SRCS_$*) -L$(OUT) -lirixcompat

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)
.PHONY: all clean

$(OUT)/init: $(OUT)/init.c
