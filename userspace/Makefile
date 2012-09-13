CC = i686-pc-toaru-gcc
CPP = i686-pc-toaru-g++
CFLAGS = -std=c99 -U__STRICT_ANSI__ -O3 -m32 -Wa,--32
CPPFLAGS = -O3 -m32 -Wa,--32
EXTRAFLAGS = -g
EXECUTABLES = $(patsubst %.c,%.o,$(wildcard lib/*.c)) $(patsubst %.c,../hdd/bin/%,$(wildcard *.c)) $(patsubst %.cpp,../hdd/bin/%,$(wildcard *.cpp))

BEG = ../util/mk-beg
END = ../util/mk-end
INFO = ../util/mk-info
ERRORS = 2>>/tmp/.`whoami`-build-errors || ../util/mk-error
ERRORSS = >>/tmp/.`whoami`-build-errors || ../util/mk-error

BEGRM = ../util/mk-beg-rm
ENDRM = ../util/mk-end-rm

TOOLCHAIN= ../toolchain/local/i686-pc-toaru

FREETYPE_INC = -I ${TOOLCHAIN}/include/freetype2/
FREETYPE_LIB = ${TOOLCHAIN}/lib/libfreetype.a
LIBPNG = ${TOOLCHAIN}/lib/libpng.a
LIBM = ${TOOLCHAIN}/lib/libm.a
LIBZ = ${TOOLCHAIN}/lib/libz.a

TARGETDIR = ../hdd/bin/
ETARGETS = terminal login compositor view game-win drawlines glogin julia-win solver wallpaper panel glock clock-win draw test-gfx threadtest bim
FTARGETS = $(ETARGETS:%=$(TARGETDIR)%)

.PHONY: all clean

all: ${EXECUTABLES}

clean:
	@${BEGRM} "RM" "Cleaning userspace full-toolchain applications."
	@-rm -f ${EXECUTABLES}
	@${ENDRM} "RM" "Cleaned userspace full-toolchain applications."

${FTARGETS}: $(TARGETDIR)% : %.c lib/graphics.o lib/list.o lib/window.o lib/sha2.o lib/decorations.o lib/pthread.o lib/shmemfonts.o lib/wcwidth.o
	@${BEG} "CC" "$@ $< [w/libs]"
	@${CC} -flto ${CFLAGS} ${EXTRAFLAGS} ${FREETYPE_INC} -o $@ $< lib/graphics.o lib/list.o lib/window.o lib/sha2.o lib/decorations.o lib/pthread.o lib/shmemfonts.o lib/wcwidth.o ${LIBM} ${FREETYPE_LIB} ${LIBPNG} ${LIBZ} ${ERRORS}
	@${END} "CC" "$< [w/libs]"

$(TARGETDIR)%: %.cpp
	@${BEG} "CPP" "$<"
	@${CPP} ${CPPFLAGS} ${EXTRAFLAGS} -o $@ $< ${ERRORS}
	@${END} "CPP" "$<"

$(TARGETDIR)ld: ld.c lib/ldlib.o
	@${BEG} "CC" "$< [-fPIC]"
	@${CC} ${CFLAGS} ${EXTRAFLAGS} -fPIC -o $@ $< lib/ldlib.o ${ERRORS}
	@${END} "CC" "$< [-fPIC]"

$(TARGETDIR)%: %.c
	@${BEG} "CC" "$<"
	@${CC} ${CFLAGS} ${EXTRAFLAGS} -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

lib/shmemfonts.o: lib/shmemfonts.c
	@${BEG} "CC" "$< [lib]"
	@${CC} ${CFLAGS} -c ${EXTRAFLAGS} ${FREETYPE_INC} -o $@ $< ${ERRORS}
	@${END} "CC" "$< [lib]"

%.o: %.c
	@${BEG} "CC" "$< [lib]"
	@${CC} ${CFLAGS} -c ${EXTRAFLAGS} -o $@ $< ${ERRORS}
	@${END} "CC" "$< [lib]"
