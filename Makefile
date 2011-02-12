#CC = gcc
CC = clang
CFLAGS = -Wall -Wextra -pedantic -m32 -O0 -std=c99 -finline-functions -fno-stack-protector -nostdinc -ffreestanding -Wno-unused-function -Wno-unused-parameter
LD = ld -m elf_i386
NASM = nasm -f elf
ECHO = `which echo` -e
MODULES = $(patsubst %.c,%.o,$(wildcard kernel/core/*.c))
FILESYSTEMS = $(patsubst %.c,%.o,$(wildcard kernel/core/fs/*.c))
EMU = qemu
GENEXT = genext2fs

.PHONY: all clean install run

all: toaruos-kernel toaruos-initrd

install: toaruos-kernel toaruos-initrd
	@${ECHO} -n "\033[34m   --   Installing to /boot...\033[0m"
	@cp toaruos-kernel /boot/toaruos-kernel
	@cp toaruos-initrd /boot/toaruos-initrd
	@${ECHO} "\r\033[34;1m   --   Kernel and ramdisk installed.\033[0m"

run: toaruos-kernel toaruos-initrd
	${EMU} -kernel toaruos-kernel -initrd toaruos-initrd -serial stdio

toaruos-kernel: kernel/start.o kernel/link.ld kernel/main.o ${MODULES} ${FILESYSTEMS}
	@${ECHO} -n "\033[32m   LD   $<\033[0m"
	@${LD} -T kernel/link.ld -o toaruos-kernel kernel/*.o kernel/core/*.o kernel/core/fs/*.o
	@${ECHO} "\r\033[32;1m   LD   $<\033[0m"

kernel/start.o: kernel/start.asm
	@${ECHO} -n "\033[32m  nasm  kernel/start.asm\033[0m"
	@${NASM} -o kernel/start.o kernel/start.asm
	@${ECHO} "\r\033[32;1m  nasm  kernel/start.asm\033[0m"

%.o: %.c
	@${ECHO} -n "\033[32m   CC   $<\033[0m"
	@${CC} ${CFLAGS} -I./kernel/include -c -o $@ $<
	@${ECHO} "\r\033[32;1m   CC   $<\033[0m"

toaruos-initrd: initrd
	@${ECHO} -n "\033[32m initrd  Generating initial RAM disk\033[0m"
	@-rm -f toaruos-initrd
	@${GENEXT} -d initrd -q -b 249 toaruos-initrd
	@${ECHO} "\r\033[32;1m initrd  Generated initial RAM disk image\033[0m"

clean:
	@${ECHO} -n "\033[31m   RM   Cleaning...\033[0m"
	@-rm -f toaruos-kernel
	@-rm -f toaruos-initrd
	@-rm -f kernel/*.o
	@-rm -f kernel/core/*.o
	@-rm -f kernel/core/fs/*.o
	@${ECHO} "\r\033[31;1m   RM   Finished cleaning.\033[0m"
