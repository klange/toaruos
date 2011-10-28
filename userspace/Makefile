CC = i686-pc-toaru-gcc
CFLAGS = -march=core2 -std=c99 -O3 -m32 -Wa,--32
EXECUTABLES = $(patsubst %.c,../initrd/bin/%,$(wildcard *.c))
ERRORS = 2>>/tmp/.build-errors || util/mk-error


.PHONY: all clean

all: ${EXECUTABLES}

clean:
	-rm -f ${EXECUTABLES}

../initrd/bin/%: %.c
	${CC} ${CFLAGS} -o $@ $<
