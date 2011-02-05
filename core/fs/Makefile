include ../../Makefile.inc
.PHONY: all clean install

all: ext2_initrd.o

%.o: %.c
	@${ECHO} -n "\e[32m   CC   core/fs/$<\e[0m"
	@${CC} ${CFLAGS} -I../../include -c -o $@ $<
	@${ECHO} "\r\e[32;1m   CC   core/fs/$<\e[0m"

clean:
	-rm -f *.o
