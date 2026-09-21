obj-m += irix_spty.o
irix_spty-y := spty/irix_spty.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# CachyOS kernels are commonly built with LLVM/Clang.  Kbuild records
# compiler-specific flags in the configured kernel tree, so external modules
# must use the same compiler family instead of falling back to gcc.
KBUILD_COMPILER := $(shell 	if grep -qs '^CONFIG_CC_IS_CLANG=y' "$(KDIR)/.config" 2>/dev/null; then 		printf '%s' 'LLVM=1'; 	fi)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) $(KBUILD_COMPILER) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) $(KBUILD_COMPILER) clean
