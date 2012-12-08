CC = i686-pc-toaru-gcc
CPP = i686-pc-toaru-g++
STRIP = i686-pc-toaru-strip
CFLAGS = -std=c99 -U__STRICT_ANSI__ -O3 -m32 -Wa,--32
CPPFLAGS = -O3 -m32 -Wa,--32
EXTRAFLAGS = -s
EXECUTABLES = $(patsubst %.c,%.o,$(wildcard lib/*.c)) $(patsubst %.c,../hdd/bin/%,$(wildcard *.c)) $(patsubst %.cpp,../hdd/bin/%,$(wildcard *.cpp))

BEG = ../util/mk-beg
END = ../util/mk-end
INFO = ../util/mk-info
ERRORS = 2>>/tmp/.`whoami`-build-errors || ../util/mk-error
ERRORSS = >>/tmp/.`whoami`-build-errors || ../util/mk-error

BEGRM = ../util/mk-beg-rm
ENDRM = ../util/mk-end-rm

FREETYPE_INC = -I ${TOOLCHAIN}/include/freetype2/
FREETYPE_LIB = ${TOOLCHAIN}/lib/libfreetype.a
LIBPNG = ${TOOLCHAIN}/lib/libpng.a
LIBM = ${TOOLCHAIN}/lib/libm.a
LIBZ = ${TOOLCHAIN}/lib/libz.a

TARGETDIR = ../hdd/bin/
ETARGETS = terminal login compositor view game drawlines glogin julia solver wallpaper panel glock clock-win draw test-gfx threadtest bim blur_test test-borders serial-console
FTARGETS = $(ETARGETS:%=$(TARGETDIR)%)

LOCAL_LIBS = $(patsubst %.c,%.o,$(wildcard lib/*.c))

.PHONY: all clean

all: ${EXECUTABLES}

clean:
	@${BEGRM} "RM" "Cleaning userspace full-toolchain applications."
	@-rm -f ${EXECUTABLES}
	@${ENDRM} "RM" "Cleaned userspace full-toolchain applications."

${FTARGETS}: $(TARGETDIR)% : %.c ${LOCAL_LIBS}
	@${BEG} "CC" "$< [w/libs]"
	@${CC} -flto ${CFLAGS} ${EXTRAFLAGS} ${FREETYPE_INC} -o $@ $< ${LOCAL_LIBS} ${LIBM} ${FREETYPE_LIB} ${LIBPNG} ${LIBZ} ${ERRORS}
	@${STRIP} $@
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

$(TARGETDIR)esh: esh.c lib/list.o lib/kbd.o
	@${BEG} "CC" "$<"
	@${CC} ${CFLAGS} ${EXTRAFLAGS} -o $@ $< lib/list.o lib/kbd.o ${ERRORS}
	@${END} "CC" "$<"

lib/shmemfonts.o: lib/shmemfonts.c
	@${BEG} "CC" "$< [lib]"
	@${CC} ${CFLAGS} -c ${EXTRAFLAGS} ${FREETYPE_INC} -o $@ $< ${ERRORS}
	@${END} "CC" "$< [lib]"

%.o: %.c
	@${BEG} "CC" "$< [lib]"
	@${CC} ${CFLAGS} -c ${EXTRAFLAGS} -o $@ $< ${ERRORS}
	@${END} "CC" "$< [lib]"
