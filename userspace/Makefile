CC = i686-pc-toaru-gcc
CPP = i686-pc-toaru-g++
CFLAGS = -march=core2 -std=c99 -O3 -m32 -Wa,--32 -Xlinker --eh-frame-hdr
CPPFLAGS = -march=core2 -O3 -m32 -Wa,--32
EXECUTABLES = $(patsubst %.c,%.o,$(wildcard lib/*.c)) $(patsubst %.c,../hdd/bin/%,$(wildcard *.c)) $(patsubst %.cpp,../hdd/bin/%,$(wildcard *.cpp))

BEG = ../util/mk-beg
END = ../util/mk-end
INFO = ../util/mk-info
ERRORS = 2>>/tmp/.`whoami`-build-errors || ../util/mk-error
ERRORSS = >>/tmp/.`whoami`-build-errors || ../util/mk-error

BEGRM = ../util/mk-beg-rm
ENDRM = ../util/mk-end-rm


.PHONY: all clean

all: ${EXECUTABLES}

clean:
	@${BEGRM} "RM" "Cleaning userspace full-toolchain applications."
	@-rm -f ${EXECUTABLES}
	@${ENDRM} "RM" "Cleaned userspace full-toolchain applications."

../hdd/bin/freetype_test: freetype_test.c
	@${BEG} "CC" "$< [freetype]"
	@${CC} ${CFLAGS} -s -I ../util/toaru-toolchain/i686-pc-toaru/include/freetype2/ -o $@ $< ../util/toaru-toolchain/i686-pc-toaru/lib/libfreetype.a ${ERRORS}
	@${END} "CC" "$< [freetype]"

../hdd/bin/terminal: terminal.c lib/utf8_decode.o
	@${BEG} "CC" "$< [freetype]"
	@${CC} ${CFLAGS} -s -I ../util/toaru-toolchain/i686-pc-toaru/include/freetype2/ -o $@ $< ../util/toaru-toolchain/i686-pc-toaru/lib/libfreetype.a lib/utf8_decode.o ${ERRORS}
	@${END} "CC" "$< [freetype]"

../hdd/bin/login: login.c lib/sha2.o
	@${BEG} "CC" "$< [w/libs]"
	@${CC} ${CFLAGS} -s -o $@ $< lib/sha2.o ${ERRORS}
	@${END} "CC" "$< [w/libs]"

../hdd/bin/compositor: compositor.c lib/graphics.o lib/list.o
	@${BEG} "CC" "$< [w/libs]"
	@${CC} ${CFLAGS} -s -I ../util/toaru-toolchain/i686-pc-toaru/include/freetype2/ -o $@ $< lib/graphics.o lib/list.o ../util/toaru-toolchain/i686-pc-toaru/lib/libfreetype.a ${ERRORS}
	@${END} "CC" "$< [w/libs]"

../hdd/bin/solver: solver.c lib/list.o
	@${BEG} "CC" "$< [w/libs]"
	@${CC} ${CFLAGS} -s -o $@ $< lib/list.o ${ERRORS}
	@${END} "CC" "$< [w/libs]"

../hdd/bin/%: %.cpp
	@${BEG} "CPP" "$<"
	@${CPP} ${CPPFLAGS} -s -o $@ $< ${ERRORS}
	@${END} "CPP" "$<"

../hdd/bin/ld: ld.c
	@${BEG} "CC" "$< [-fPIC]"
	@${CC} ${CFLAGS} -s -fPIC -o $@ $< ${ERRORS}
	@${END} "CC" "$< [-fPIC]"

../hdd/bin/%: %.c
	@${BEG} "CC" "$<"
	@${CC} ${CFLAGS} -s -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

%.o: %.c
	@${BEG} "CC" "$< [lib]"
	@${CC} ${CFLAGS} -c -s -o $@ $< ${ERRORS}
	@${END} "CC" "$< [lib]"
