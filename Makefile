APPS=init hello sh ls terminal uname compositor drawlines background session kdebug cat yutani-test sysinfo hostname yutani-query

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

base/lib/ld.so: linker/linker.c base/lib/libnihc.a | dirs
	$(CC) -static -Wl,-static $(CFLAGS) -o $@ -Os -T linker/link.ld $< $(LIBS)

base/lib/libnihc.a: ${LIBC_OBJS} | dirs
	$(AR) cr $@ $^

base/lib/libnihc.so: ${LIBC_OBJS} | dirs
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $^

base/lib/libtoaru_graphics.so: lib/graphics.c lib/graphics.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_list.so: lib/list.c lib/list.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_tree.so: lib/tree.c lib/tree.h base/lib/libtoaru_list.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_list

base/lib/libtoaru_hashmap.so: lib/hashmap.c lib/hashmap.h base/lib/libtoaru_list.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_list

base/lib/libtoaru_kbd.so: lib/kbd.c lib/kbd.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_pthread.so: lib/pthread.c lib/pthread.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_pex.so: lib/pex.c lib/pex.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_dlfcn.so: lib/dlfcn.c lib/dlfcn.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_yutani.so: lib/yutani.c lib/yutani.h base/lib/libtoaru_graphics.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_graphics

base/lib/libtoaru_rline.so: lib/rline.c lib/rline.h base/lib/libtoaru_kbd.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_kbd

base/lib/libtoaru_termemu.so: lib/termemu.c lib/termemu.h base/lib/libtoaru_graphics.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_graphics

base/lib/libtoaru_decorations.so: lib/decorations.c lib/decorations.h base/lib/libtoaru_graphics.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_graphics

base/lib/libtoaru-decor-fancy.so: decors/decor-fancy.c lib/decorations.h base/lib/libtoaru_graphics.so base/lib/libtoaru_decorations.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_decorations -ltoaru_graphics

base/bin/init: init.c base/lib/libnihc.a | dirs
	$(CC) -static -Wl,-static $(CFLAGS) -o $@ $< $(LIBS)

base/bin/sh: sh.c base/lib/libnihc.so base/lib/libtoaru_list.so base/lib/libtoaru_rline.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_rline -ltoaru_list -ltoaru_kbd $(LIBS)

base/bin/sysinfo: sysinfo.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_termemu.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_graphics -ltoaru_termemu $(LIBS)

base/bin/terminal: terminal.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_decorations.so base/lib/libtoaru_dlfcn.so base/lib/libtoaru_list.so base/lib/libtoaru_kbd.so base/lib/libtoaru_termemu.so base/lib/libtoaru_pex.so base/lib/libtoaru_hashmap.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_termemu -ltoaru_decorations -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_hashmap -ltoaru_dlfcn -ltoaru_kbd -ltoaru_list $(LIBS)

base/bin/background: background.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/drawlines: drawlines.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/yutani-query: yutani-query.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/yutani-test: yutani-test.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/compositor: compositor.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_list.so base/lib/libtoaru_kbd.so base/lib/libtoaru_pthread.so base/lib/libtoaru_pex.so base/lib/libtoaru_yutani.so base/lib/libtoaru_hashmap.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_pthread -ltoaru_pex -ltoaru_graphics -ltoaru_kbd -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/ls: ls.c base/lib/libnihc.so base/lib/libtoaru_list.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_list $(LIBS)

base/bin/%: %.c base/lib/libnihc.so | dirs
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

cdrom/ramdisk.img.gz: ${APPS_X} base/lib/ld.so base/lib/libtoaru-decor-fancy.so | dirs
	genext2fs -B 4096 -d base -U -b 16384 -N 2048 cdrom/ramdisk.img
	rm -f cdrom/ramdisk.img.gz
	gzip cdrom/ramdisk.img

image.iso: cdrom/ramdisk.img.gz
	grub-mkrescue -d /usr/lib/grub/i386-pc --compress=xz -o $@ cdrom

.PHONY: clean
clean:
	rm -f base/lib/*.so
	rm -f base/lib/libnihc.a
	rm -f libc/*.o
	rm -f image.iso
	rm -f cdrom/randisk.img.gz
