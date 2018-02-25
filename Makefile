APPS=init hello sh ls

CC=i686-pc-toaru-gcc
AR=i686-pc-toaru-ar
CFLAGS=-nodefaultlibs -O3 -m32 -Wa,--32 -g -std=c99 -I. -isystem include -Lbase/lib
LIBS=-lnihc -lgcc

LIBC_OBJS=$(patsubst %.c,%.o,$(wildcard libc/*.c))

APPS_X=$(foreach app,$(APPS),base/bin/$(app))

all: image.iso

base/dev:
	mkdir -p base/dev
base/tmp:
	mkdir -p base/tmp
base/proc:
	mkdir -p base/proc
base/bin:
	mkdir -p base/bin
base/lib:
	mkdir -p base/lib
dirs: base/dev base/tmp base/proc base/bin base/lib

libc/%.o: libc/%.c
	$(CC) -fPIC -c -m32 -Wa,--32 -O3 -isystem include -o $@ $<

base/lib/ld.so: linker/linker.c base/lib/libnihc.a
	$(CC) -static -Wl,-static $(CFLAGS) -o $@ -Os -T linker/link.ld $< $(LIBS)

base/lib/libnihc.a: ${LIBC_OBJS}
	$(AR) cr $@ $^

base/lib/libnihc.so: ${LIBC_OBJS}
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $^

base/lib/libtoaru_graphics.so: lib/graphics.c lib/graphics.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_list.so: lib/list.c lib/list.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_kbd.so: lib/kbd.c lib/kbd.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_rline.so: lib/rline.c lib/rline.h base/lib/libtoaru_kbd.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< base/lib/libtoaru_kbd.so

base/bin/init: init.c base/lib/libnihc.a | dirs
	$(CC) -static -Wl,-static $(CFLAGS) -o $@ $< $(LIBS)

base/bin/sh: sh.c base/lib/libnihc.so base/lib/libtoaru_list.so base/lib/libtoaru_rline.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_rline -ltoaru_list -ltoaru_kbd $(LIBS)

base/bin/hello: hello.c base/lib/libnihc.so base/lib/libtoaru_graphics.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_graphics $(LIBS)

base/bin/ls: ls.c base/lib/libnihc.so base/lib/libtoaru_list.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_list $(LIBS)

base/bin/%: %.c base/lib/libnihc.so | dirs
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

cdrom/ramdisk.img.gz: ${APPS_X} base/lib/ld.so | dirs
	genext2fs -B 4096 -d base -U -b 16384 -N 2048 cdrom/ramdisk.img
	rm -f cdrom/ramdisk.img.gz
	gzip cdrom/ramdisk.img

image.iso: cdrom/ramdisk.img.gz
	grub-mkrescue -d /usr/lib/grub/i386-pc --compress=xz -o $@ cdrom

