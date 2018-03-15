APPS=init hello sh ls terminal uname compositor drawlines background session kdebug cat yutani-test sysinfo hostname yutani-query env mount date echo nyancat kill ps pstree bim terminal-vga cursor-off font-server migrate free uptime

KERNEL_TARGET=i686-elf
KCC = $(KERNEL_TARGET)-gcc
KAS = $(KERNEL_TARGET)-as
KLD = $(KERNEL_TARGET)-ld

CC=i686-pc-toaru-gcc
AR=i686-pc-toaru-ar
CFLAGS=-nodefaultlibs -O3 -m32 -Wa,--32 -g -std=c99 -I. -Iapps -isystem include -Lbase/lib
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
cdrom/boot:
	mkdir -p cdrom/boot
dirs: base/dev base/tmp base/proc base/bin base/lib cdrom/boot

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

base/lib/libtoaru_drawstring.so: lib/drawstring.c lib/drawstring.h base/lib/libtoaru_graphics.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_graphics

base/lib/libtoaru_decorations.so: lib/decorations.c lib/decorations.h base/lib/libtoaru_graphics.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_graphics

base/lib/libtoaru-decor-fancy.so: decors/decor-fancy.c lib/decorations.h base/lib/libtoaru_graphics.so base/lib/libtoaru_decorations.so base/lib/libtoaru_drawstring.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_decorations -ltoaru_drawstring -ltoaru_graphics

base/bin/init: apps/init.c base/lib/libnihc.a | dirs
	$(CC) -static -Wl,-static $(CFLAGS) -o $@ $< $(LIBS)

base/bin/sh: apps/sh.c base/lib/libnihc.so base/lib/libtoaru_list.so base/lib/libtoaru_rline.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_rline -ltoaru_list -ltoaru_kbd $(LIBS)

base/bin/migrate: apps/migrate.c base/lib/libnihc.so base/lib/libtoaru_list.so base/lib/libtoaru_hashmap.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/sysinfo: apps/sysinfo.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_termemu.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_graphics -ltoaru_termemu $(LIBS)

base/bin/terminal: apps/terminal.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_decorations.so base/lib/libtoaru_dlfcn.so base/lib/libtoaru_list.so base/lib/libtoaru_kbd.so base/lib/libtoaru_termemu.so base/lib/libtoaru_pex.so base/lib/libtoaru_hashmap.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_termemu -ltoaru_decorations -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_hashmap -ltoaru_dlfcn -ltoaru_kbd -ltoaru_list $(LIBS)

base/bin/terminal-vga: apps/terminal-vga.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_kbd.so base/lib/libtoaru_termemu.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_termemu -ltoaru_graphics -ltoaru_kbd $(LIBS)

base/bin/background: apps/background.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so base/lib/libtoaru_drawstring.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_drawstring -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/drawlines: apps/drawlines.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/yutani-query: apps/yutani-query.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/yutani-test: apps/yutani-test.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/compositor: apps/compositor.c base/lib/libnihc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_list.so base/lib/libtoaru_kbd.so base/lib/libtoaru_pthread.so base/lib/libtoaru_pex.so base/lib/libtoaru_yutani.so base/lib/libtoaru_hashmap.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_pthread -ltoaru_pex -ltoaru_graphics -ltoaru_kbd -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/ls: apps/ls.c base/lib/libnihc.so base/lib/libtoaru_list.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_list $(LIBS)

base/bin/nyancat: apps/nyancat/nyancat.c base/lib/libnihc.so
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

base/bin/ps: apps/ps.c base/lib/libnihc.so base/lib/libtoaru_list.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_list $(LIBS)

base/bin/pstree: apps/pstree.c base/lib/libnihc.so base/lib/libtoaru_tree.so base/lib/libtoaru_list.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_tree -ltoaru_list $(LIBS)

base/bin/%: apps/%.c base/lib/libnihc.so | dirs
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

cdrom/ramdisk.img: ${APPS_X} base/lib/ld.so base/lib/libtoaru-decor-fancy.so Makefile | dirs
	genext2fs -B 4096 -d base -U -b 4096 -N 2048 cdrom/ramdisk.img

image.iso: cdrom/ramdisk.img cdrom/boot/boot.sys cdrom/kernel
	xorriso -as mkisofs -R -J -c boot/bootcat -b boot/boot.sys -no-emul-boot -boot-load-size 20 -o image.iso cdrom

cdrom/boot/boot.sys: boot/boot.o boot/cstuff.o boot/link.ld | cdrom/boot
	${KLD} -T boot/link.ld -o $@ boot/boot.o boot/cstuff.o

boot/cstuff.o: boot/cstuff.c boot/ata.h  boot/atapi_imp.h  boot/elf.h  boot/iso9660.h  boot/multiboot.h  boot/text.h  boot/types.h  boot/util.h
	${KCC} -c -Os -o $@ $<

boot/boot.o: boot/boot.s
	yasm -f elf -o $@ $<

.PHONY: clean
clean:
	rm -f base/lib/*.so
	rm -f base/lib/libnihc.a
	rm -f ${APPS_X}
	rm -f libc/*.o
	rm -f image.iso
	rm -f cdrom/ramdisk.img
	rm -f cdrom/boot/boot.sys
	rm -f boot/*.o
