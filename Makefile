include Makefile.inc

DIRS = core

.PHONY: all clean install run curses initrd core

all: kernel

install: kernel
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

kernel: start.o link.ld main.o core
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

core:
	@cd core; ${MAKE} ${MFLAGS}

initrd: fs
	@${ECHO} -n "\e[32m initrd  Generating initial RAM disk\e[0m"
	@-rm -f initrd
	@genext2fs -d fs -q -b 249 initrd
	@${ECHO} "\r\e[32;1m initrd  Generated initial RAM disk image\e[0m"

clean:
	@-rm -f *.o kernel
	@-rm -f bootdisk.img
	@-rm -f initrd
	@-rm -f core.d
	@-for d in ${DIRS}; do (cd $$d; ${MAKE} clean); done
