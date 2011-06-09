CC = i686-pc-toaru-gcc
CFLAGS = -march=core2 -std=c99 -O3 -m32 -Wa,--32

.PHONY: all

all: ../initrd/bin/game ../initrd/bin/julia ../initrd/bin/kbd-test

../initrd/bin/game: main.c
	${CC} ${CFLAGS} -o $@ $<

../initrd/bin/julia: julia.c
	${CC} ${CFLAGS} -o $@ $<

../initrd/bin/kbd-test: keyboard-test.c
	${CC} ${CFLAGS} -o $@ $<

