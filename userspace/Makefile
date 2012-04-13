CC = i686-pc-toaru-gcc
CPP = i686-pc-toaru-g++
CFLAGS = -std=c99 -O3 -m32 -Wa,--32 -Xlinker --eh-frame-hdr
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

FREETYPE_INC = -I ../util/toaru-toolchain/i686-pc-toaru/include/freetype2/
FREETYPE_LIB = ../util/toaru-toolchain/i686-pc-toaru/lib/libfreetype.a
LIBM = ../util/toaru-toolchain/i686-pc-toaru/lib/libm.a

TARGETDIR = ../hdd/bin/
ETARGETS = terminal login compositor view game-win drawlines glogin julia-win solver donut wallpaper panel glock clock-win draw
FTARGETS = $(ETARGETS:%=$(TARGETDIR)%)

.PHONY: all clean

all: ${EXECUTABLES}

clean:
	@${BEGRM} "RM" "Cleaning userspace full-toolchain applications."
	@-rm -f ${EXECUTABLES}
	@${ENDRM} "RM" "Cleaned userspace full-toolchain applications."

${FTARGETS}: $(TARGETDIR)% : %.c lib/graphics.o lib/list.o lib/window.o lib/sha2.o lib/decorations.o lib/pthread.o
	@${BEG} "CC" "$@ $< [w/libs]"
	@${CC} ${CFLAGS} ${EXTRAFLAGS} ${FREETYPE_INC} -o $@ $< lib/graphics.o lib/list.o lib/window.o lib/sha2.o lib/decorations.o lib/pthread.o ${LIBM} ${FREETYPE_LIB} ${ERRORS}
	@${END} "CC" "$< [w/libs]"

$(TARGETDIR)%: %.cpp
	@${BEG} "CPP" "$<"
	@${CPP} ${CPPFLAGS} ${EXTRAFLAGS} -o $@ $< ${ERRORS}
	@${END} "CPP" "$<"

$(TARGETDIR)ld: ld.c
	@${BEG} "CC" "$< [-fPIC]"
	@${CC} ${CFLAGS} ${EXTRAFLAGS} -fPIC -o $@ $< ${ERRORS}
	@${END} "CC" "$< [-fPIC]"

$(TARGETDIR)%: %.c
	@${BEG} "CC" "$<"
	@${CC} ${CFLAGS} ${EXTRAFLAGS} -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

lib/decorations.o: lib/decorations.c
	@${BEG} "CC" "$< [lib]"
	@${CC} ${CFLAGS} -c ${EXTRAFLAGS} ${FREETYPE_INC} -o $@ $< ${ERRORS}
	@${END} "CC" "$< [lib]"

%.o: %.c
	@${BEG} "CC" "$< [lib]"
	@${CC} ${CFLAGS} -c ${EXTRAFLAGS} -o $@ $< ${ERRORS}
	@${END} "CC" "$< [lib]"
