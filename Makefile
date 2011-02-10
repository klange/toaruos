CC = gcc
LD = ld -m elf_i386
CFLAGS = -Wall -Wextra -pedantic -m32 -O0 -std=c99 -finline-functions -fno-stack-protector -nostdinc -ffreestanding -Wno-unused-function -Wno-unused-parameter
NASM = nasm -f elf
ECHO = `which echo` -e
MODULES = $(patsubst %.c,%.o,$(wildcard core/*.c))
FILESYSTEMS = $(patsubst %.c,%.o,$(wildcard core/fs/*.c))
EMU = qemu
GENEXT = genext2fs

.PHONY: all clean install run

all: kernel initrd

install: kernel initrd
	@${ECHO} -n "\033[34m   --   Installing to /boot...\033[0m"
	@cp kernel /boot/toaruos-kernel
	@cp initrd /boot/toaruos-initrd
	@${ECHO} "\r\033[34;1m   --   Kernel and ramdisk installed.\033[0m"

run: kernel initrd
	${EMU} -kernel kernel -initrd initrd

kernel: start.o link.ld main.o ${MODULES} ${FILESYSTEMS}
	@${ECHO} -n "\033[32m   LD   $<\033[0m"
	@${LD} -T link.ld -o kernel *.o core/*.o core/fs/*.o
	@${ECHO} "\r\033[32;1m   LD   $<\033[0m"

start.o: start.asm
	@${ECHO} -n "\033[32m  nasm  start.asm\033[0m"
	@${NASM} -o start.o start.asm
	@${ECHO} "\r\033[32;1m  nasm  start.asm\033[0m"

%.o: %.c
	@${ECHO} -n "\033[32m   CC   $<\033[0m"
	@${CC} ${CFLAGS} -I./include -c -o $@ $<
	@${ECHO} "\r\033[32;1m   CC   $<\033[0m"

initrd: fs
	@${ECHO} -n "\033[32m initrd  Generating initial RAM disk\033[0m"
	@-rm -f initrd
	@${GENEXT} -d fs -q -b 249 initrd
	@${ECHO} "\r\033[32;1m initrd  Generated initial RAM disk image\033[0m"

clean:
	@${ECHO} -n "\033[31m   RM   Cleaning...\033[0m"
	@-rm -f *.o kernel
	@-rm -f bootdisk.img
	@-rm -f initrd
	@-rm -f core/*.o
	@-rm -f core/fs/*.o
	@${ECHO} "\r\033[31;1m   RM   Finished cleaning.\033[0m"
