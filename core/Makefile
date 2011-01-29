include ../Makefile.inc
.PHONY: all clean install fs

all: system.o multiboot.o gdt.o idt.o irq.o isrs.o kbd.o kprintf.o timer.o vga.o mem.o panic.o alloc.o vfs.o fs

%.o: %.c
	${CC} ${CFLAGS} -I../include -c -o $@ $<

fs:
	cd fs; ${MAKE} ${MFLAGS}

clean:
	-rm -f *.o
