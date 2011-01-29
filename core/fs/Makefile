include ../../Makefile.inc
.PHONY: all clean install

all: ext2_initrd.o

%.o: %.c
	${CC} ${CFLAGS} -I../../include -c -o $@ $<

clean:
	-rm -f *.o
