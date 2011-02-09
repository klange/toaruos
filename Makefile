CC = gcc
LD = ld -m elf_i386
CFLAGS = -Wall -Wextra -pedantic -m32 -O0 -std=c99 -finline-functions -fno-stack-protector -nostdinc -ffreestanding -Wno-unused-function -Wno-unused-parameter
NASM = nasm -f elf
ECHO = `which echo` -e
MODULES = $(patsubst %.c,%.o,$(wildcard core/*.c))
FILESYSTEMS = $(patsubst %.c,%.o,$(wildcard core/fs/*.c))

.PHONY: all clean install run curses initrd corr

all: kernel initrd

install: kernel initrd
	@${ECHO} -n "\e[32m   --   Installing floppy image...\e[0m"
	@cp bootdisk.src.img bootdisk.img
	@mount bootdisk.img /mnt -o loop
	@cp kernel /mnt/kernel
	@cp initrd /mnt/initrd
	@umount /mnt
	@cp kernel /boot/toaruos-kernel
	@cp initrd /boot/toaruos-initrd
	@${ECHO} "\r\e[32;1m   --   Floppy image created.     \e[0m"

run: bootdisk.img
	qemu -fda bootdisk.img

curses: bootdisk.img
	@qemu -curses -fda bootdisk.img

kernel: start.o link.ld main.o ${MODULES} ${FILESYSEMS}
	@${ECHO} -n "\e[32m   LD   $<\e[0m"
	@${LD} -T link.ld -o kernel *.o core/*.o core/fs/*.o
	@${ECHO} "\r\e[32;1m   LD   $<\e[0m"

start.o: start.asm
	@${ECHO} -n "\e[32m  nasm  start.asm\e[0m"
	@nasm -f elf -o start.o start.asm
	@${ECHO} "\r\e[32;1m  nasm  start.asm\e[0m"

%.o: %.c
	@${ECHO} -n "\e[32m   CC   $<\e[0m"
	@${CC} ${CFLAGS} -I./include -c -o $@ $<
	@${ECHO} "\r\e[32;1m   CC   $<\e[0m"

initrd: fs
	@${ECHO} -n "\e[32m initrd  Generating initial RAM disk\e[0m"
	@-rm -f initrd
	@genext2fs -d fs -q -b 249 initrd
	@${ECHO} "\r\e[32;1m initrd  Generated initial RAM disk image\e[0m"

clean:
	@-rm -f *.o kernel
	@-rm -f bootdisk.img
	@-rm -f initrd
	@-rm -f core/*.o
	@-rm -f core/fs/*.o
