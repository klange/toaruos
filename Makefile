include Makefile.inc

DIRS = core

.PHONY: all clean install core run curses

all: kernel initrd

install: kernel
	cp bootdisk.src.img bootdisk.img
	mount bootdisk.img /mnt -o loop
	cp kernel /mnt/kernel
	cp initrd /mnt/initrd
	umount /mnt
	cp kernel /boot/toaruos-kernel
	cp initrd /boot/toaruos-initrd

run: bootdisk.img
	qemu -fda bootdisk.img

curses: bootdisk.img
	qemu -curses -fda bootdisk.img

kernel: start.o link.ld main.o core
	${LD} -T link.ld -o kernel *.o core/*.o core/fs/*.o

%.o: %.c
	${CC} ${CFLAGS} -I./include -c -o $@ $<

core:
	cd core; ${MAKE} ${MFLAGS}

start.o: start.asm
	nasm -f elf -o start.o start.asm

initrd: fs
	genext2fs -d fs -q -b 249 -v initrd

clean:
	-rm -f *.o kernel
	-rm -f bootdisk.img
	-rm -f initrd
	-for d in ${DIRS}; do (cd $$d; ${MAKE} clean); done
