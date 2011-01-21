include ../Makefile.inc
.PHONY: all clean install core

all: gdt.o idt.o irq.o isrs.o kbd.o kprintf.o timer.o vga.o mem.o panic.o

%.o: %.c
	${CC} ${CFLAGS} -I../include -c -o $@ $<

clean:
	-rm -f *.o
