CC = i686-pc-toaru-gcc
CPP = i686-pc-toaru-g++
CFLAGS = -std=c99 -U__STRICT_ANSI__ -O3 -m32 -Wa,--32
CPPFLAGS = -O3 -m32 -Wa,--32
EXTRAFLAGS = -g

EXECUTABLES = $(patsubst %.c,../hdd/bin/%,$(wildcard *.c))

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

F_INCLUDES = ${FREETYPE_INC}
F_LIBRARIES = ${FREETYPE_LIB} ${LIBPNG} ${LIBM} ${LIBZ}

CAIRO_INC = -I ${TOOLCHAIN}/include/cairo/
CAIRO_LIB = ${TOOLCHAIN}/lib/libcairo.a
PIXMAN_INC = -I ${TOOLCHAIN}/include/pixman-1/
PIXMAN_LIB = ${TOOLCHAIN}/lib/libpixman-1.a

EXTRA_LIB_INCLUDES  = ${F_INCLUDES} ${CAIRO_INC} ${PIXMAN_INC}
EXTRA_LIB_LIBRARIES = ${F_LIBRARIES} ${CAIRO_LIB} ${PIXMAN_LIB}

LOCAL_LIBS = $(patsubst %.c,%.o,$(wildcard ../userspace/lib/*.c))
LOCAL_INC  = -I ../userspace/

TARGETDIR = ../hdd/bin/
EXTRA_LIB_APPS = compositor2 cairo-demo pixman-demo
EXTRA_LIB_TARGETS = $(EXTRA_LIB_APPS:%=$(TARGETDIR)%)

.PHONY: all clean

all: ${EXECUTABLES}

clean:
	@${BEGRM} "RM" "Cleaning userspace full-toolchain applications."
	@-rm -f ${EXECUTABLES}
	@${ENDRM} "RM" "Cleaned userspace full-toolchain applications."

${EXTRA_LIB_TARGETS}: $(TARGETDIR)% : %.c
	@${BEG} "CC" "$< [w/extra libs]"
	@${CC} -flto ${CFLAGS} ${EXTRAFLAGS} ${LOCAL_INC} ${EXTRA_LIB_INCLUDES} -o $@ $< ${LOCAL_LIBS} ${EXTRA_LIB_LIBRARIES} ${ERRORS}
	@${END} "CC" "$< [w/extra libs]"

