CC=i686-pc-toaru-gcc
AR=i686-pc-toaru-ar

.PHONY: all go
all: ld.so libdemo.so demo demob libc.so

ld.so: linker.c link.ld
	i686-pc-toaru-gcc -static -Wl,-static -std=c99 -g -U__STRICT_ANSI__ -o ld.so -Os -T link.ld linker.c

demo: demo.c libc.so libdemo.so
	i686-pc-toaru-gcc -o demo -g demo.c -L. -ldemo -lc

demob: demob.c libc.so
	i686-pc-toaru-gcc -o demob demob.c -L. -lc

libdemo.so: libdemo.c libc.so
	i686-pc-toaru-gcc -shared -fPIC -Wl,-soname,libdemo.so -o libdemo.so libdemo.c -lc

libc.so: ${TOARU_SYSROOT}/usr/lib/libc.a
	cp ${TOARU_SYSROOT}/usr/lib/libc.a libc.a
	# init and fini don't belong in our shared object
	${AR} d libc.a lib_a-init.o
	${AR} d libc.a lib_a-fini.o
	# Remove references to newlib's reentrant malloc
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
	rm libc.a

