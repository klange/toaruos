CC = i686-pc-toaru-gcc
CFLAGS = -march=core2 -std=c99 -O3 -m32 -Wa,--32
EXECUTABLES = $(patsubst %.c,../hdd/bin/%,$(wildcard *.c))
ERRORS = 2>>/tmp/.build-errors || util/mk-error


.PHONY: all clean

all: ${EXECUTABLES}

clean:
	-rm -f ${EXECUTABLES}

../hdd/bin/freetype_test: freetype_test.c
	${CC} ${CFLAGS} -s -I ../util/toaru-toolchain/i686-pc-toaru/include/freetype2/ -o $@ $< ../util/toaru-toolchain/i686-pc-toaru/lib/libfreetype.a

../hdd/bin/%: %.c
	${CC} ${CFLAGS} -s -o $@ $<
