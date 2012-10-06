CC = i686-pc-toaru-gcc
CPP = i686-pc-toaru-g++
CFLAGS = -march=core2 -std=c99 -O3 -m32 -Wa,--32
CPPFLAGS = -march=core2 -O3 -m32 -Wa,--32
EXECUTABLES = $(patsubst %.c,../hdd/bin/%,$(wildcard *.c)) $(patsubst %.cpp,../hdd/bin/%,$(wildcard *.cpp))
ERRORS = 2>>/tmp/.build-errors || util/mk-error


.PHONY: all clean

all: ${EXECUTABLES}

clean:
	-rm -f ${EXECUTABLES}

../hdd/bin/freetype_test: freetype_test.c
	${CC} ${CFLAGS} -s -I ~/osdev/toolchain/local/i686-pc-toaru/include/freetype2/ -o $@ $< -lfreetype

../hdd/bin/%: %.cpp
	${CPP} ${CPPFLAGS} -s -o $@ $<

../hdd/bin/ld: ld.c
	${CC} ${CFLAGS} -s -fPIC -o $@ $<

../hdd/bin/%: %.c
	${CC} ${CFLAGS} -s -o $@ $<
