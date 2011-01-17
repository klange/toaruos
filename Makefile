include Makefile.inc

DIRS = core

.PHONY: all clean install core

all: kernel

install: kernel
	mount bootdisk.img /mnt -o loop
	cp kernel /mnt/kernel
	umount /mnt
	cp kernel /boot/toaruos-kernel

kernel: start.o link.ld main.o core
	${LD} -T link.ld -o kernel *.o core/*.o

%.o: %.c
	${CC} ${CFLAGS} -I./include -c -o $@ $<

core:
	cd core; ${MAKE} ${MFLAGS}

start.o: start.asm
	nasm -f elf -o start.o start.asm

clean:
	-rm -f *.o kernel
	-for d in ${DIRS}; do (cd $$d; ${MAKE} clean); done
