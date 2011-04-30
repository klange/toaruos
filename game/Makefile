CC = i686-pc-toaru-gcc
CFLAGS = -march=core2 -std=c99 -O3 -I/afs/acm.uiuc.edu/user/lange7/toaru-toolchain/local/i686-pc-toaru/include/freetype2

.PHONY: all

all: ../initrd/bin/game ../initrd/bin/julia

../initrd/bin/game: main.c
	${CC} ${CFLAGS} -o $@ $< ~/toaru-toolchain/local/i686-pc-toaru/lib/libfreetype.a

../initrd/bin/julia: julia.c
	${CC} ${CFLAGS} -o $@ $<

