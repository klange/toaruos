CC = i686-pc-toaru-gcc
CFLAGS = -march=core2 -std=c99 -O0 -I/afs/acm.uiuc.edu/user/lange7/toaru-toolchain/local/i686-pc-toaru/include/freetype2

.PHONY: all

all: ../initrd/bin/game

../initrd/bin/game: game
	cp game ../initrd/bin/game

game: main.c
	${CC} ${CFLAGS} -o game main.c ~/toaru-toolchain/local/i686-pc-toaru/lib/libfreetype.a
