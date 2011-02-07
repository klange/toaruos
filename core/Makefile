include ../Makefile.inc
DIRS = fs

.PHONY: all clean install fs

all: system.o multiboot.o gdt.o idt.o irq.o isrs.o kbd.o kprintf.o timer.o vga.o mem.o panic.o alloc.o vfs.o shell.o fs

%.o: %.c
	@${ECHO} -n "\e[32m   CC   core/$<\e[0m"
	@${CC} ${CFLAGS} -I../include -c -o $@ $<
	@${ECHO} "\r\e[32;1m   CC   core/$<\e[0m"

fs:
	@cd fs; ${MAKE} ${MFLAGS}

clean:
	@-rm -f *.o
	@-for d in ${DIRS}; do (cd $$d; ${MAKE} clean); done
