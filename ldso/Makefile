CC = i686-pc-toaru-gcc
AR = i686-pc-toaru-ar
CFLAGS   = -m32 -Wa,--32 -g -I../userspace -std=c99 -U__STRICT_ANSI__
LD_CFLAGS = -Tld.ld

OBJ_LIBC = ../hdd/lib/libc.so
OBJ_LDSO = ../hdd/lib/ld.so
OBJ_LIBFOO = ../hdd/lib/libfoo.so
BIN_LD_DEMO = ../hdd/bin/ld-demo

OBJS = ${OBJ_LDSO} ${OBJ_LIBFOO}
BINS = ${BIN_LD_DEMO}

all: ${OBJS} ${BINS}

${OBJ_LIBC}:
	mkdir -p ../hdd/lib
	cp ${TOARU_SYSROOT}/usr/lib/libc.a libc.a
	# init and fini don't belong in our shared object
	${AR} d libc.a lib_a-init.o
	${AR} d libc.a lib_a-fini.o
	# Kill reentrant make crap
	${AR} d libc.a lib_a-calloc.o
	${AR} d libc.a lib_a-callocr.o
	${AR} d libc.a lib_a-cfreer.o
	${AR} d libc.a lib_a-freer.o
	${AR} d libc.a lib_a-malignr.o
	${AR} d libc.a lib_a-mallinfor.o
	${AR} d libc.a lib_a-mallocr.o
	${AR} d libc.a lib_a-malloptr.o
	${AR} d libc.a lib_a-mallstatsr.o
	${AR} d libc.a lib_a-msizer.o
	${AR} d libc.a lib_a-pvallocr.o
	${AR} d libc.a lib_a-realloc.o
	${AR} d libc.a lib_a-reallocr.o
	${AR} d libc.a lib_a-vallocr.o
	${CC} -shared -o libc.so -Wl,--whole-archive libc.a -Wl,--no-whole-archive
	cp libc.so ../hdd/lib/libc.so
	rm libc.so
	rm libc.a

${OBJ_LDSO}: ld.c
	${CC} ${CFLAGS} ${LD_CFLAGS} -o $@ $<

${OBJ_LIBFOO}: libfoo.c
	${CC} ${CFLAGS} -fPIC -shared -o $@ $<

${BIN_LD_DEMO}: ld-demo.c
	${CC} ${CFLAGS} -L../hdd/lib -o $@ $< -lfoo
