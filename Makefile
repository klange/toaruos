.PHONY: all clean install

all: kernel

install: kernel
	mount bootdisk.img /mnt -o loop
	cp kernel /mnt/kernel
	umount /mnt

kernel: start.o link.ld main.o vga.o
	ld -m elf_i386 -T link.ld -o kernel start.o main.o vga.o

%.o: %.c
	gcc -Wall -m32 -O -fstrength-reduce -fomit-frame-pointer -finline-functions -nostdinc -fno-builtin -I./include -c -o $@ $<

start.o: start.asm
	nasm -f elf -o start.o start.asm

clean:
	rm -f *.o kernel
