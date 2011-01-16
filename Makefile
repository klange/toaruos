.PHONY: all clean install

all: kernel

install: kernel
	mount bootdisk.img /mnt -o loop
	cp kernel /mnt/kernel
	umount /mnt

kernel: start.o link.ld main.o vga.o gdt.o idt.o isrs.o irq.o
	ld -m elf_i386 -T link.ld -o kernel *.o

%.o: %.c
	gcc -Wall -m32 -O -fstrength-reduce -fomit-frame-pointer -finline-functions -nostdinc -fno-builtin -I./include -c -o $@ $<

start.o: start.asm
	nasm -f elf -o start.o start.asm

clean:
	rm -f *.o kernel
