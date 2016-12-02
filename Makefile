CC=i686-pc-toaru-gcc
AR=i686-pc-toaru-ar

.PHONY: all go
all: ld.so libdemo.so demo

ld.so: linker.c link.ld
	i686-pc-toaru-gcc -static -std=c99 -g -U__STRICT_ANSI__ -o ld.so -Os -T link.ld linker.c

demo: demo.c
	i686-pc-toaru-gcc -o demo -g demo.c -L. -ldemo

demob: demob.c
	i686-pc-toaru-gcc -o demob demob.c -L.

libdemo.so: libdemo.c
	i686-pc-toaru-gcc -shared -fPIC -Wl,-soname,libdemo.so -o libdemo.so libdemo.c

libc.so:
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

go: all
	cp demo ../hdd/bin/ld-demo
	cp demob ../hdd/bin/ld-demob
	mkdir -p ../hdd/usr/lib
	cp libdemo.so ../hdd/usr/lib/libdemo.so
	cp libc.so ../hdd/usr/lib/libc.so
	mkdir -p ../hdd/lib
	cp ld.so ../hdd/lib/ld.so

cd: go
	cd ..; make cdrom
	-VBoxManage controlvm "ToaruOS Live CD" poweroff
	sleep 0.2
	-VBoxManage startvm "ToaruOS Live CD"
	sleep 3
	-VBoxManage controlvm "ToaruOS Live CD" keyboardputscancode 1c 9c
	sleep 2
	-VBoxManage controlvm "ToaruOS Live CD" keyboardputscancode 38 3e be b8 1d 38 14 94 b8 9d
	sleep 0.5
	-VBoxManage controlvm "ToaruOS Live CD" keyboardputscancode 38 0f 8f b8
	sleep 0.2
	-VBoxManage controlvm "ToaruOS Live CD" keyboardputscancode 38 44 c4 b8


