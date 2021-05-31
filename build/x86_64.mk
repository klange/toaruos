ARCH=x86_64

ARCH_KERNEL_CFLAGS  = -mno-red-zone -fno-omit-frame-pointer -mfsgsbase
ARCH_KERNEL_CFLAGS += -mgeneral-regs-only -z max-page-size=0x1000 -nostdlib

TARGET=x86_64-pc-toaru
