APPS=init hello sh ls terminal uname compositor drawlines background session kdebug cat yutani-test sysinfo hostname yutani-query env mount date echo nyancat kill ps pstree bim terminal-vga cursor-off font-server migrate free uptime

KERNEL_TARGET=i686-pc-toaru
KCC = $(KERNEL_TARGET)-gcc
KAS = $(KERNEL_TARGET)-as
KLD = $(KERNEL_TARGET)-ld
KNM = $(KERNEL_TARGET)-nm

CC=i686-pc-toaru-gcc
AR=i686-pc-toaru-ar
CFLAGS= -O3 -m32 -Wa,--32 -g -std=c99 -I. -Iapps 
LIBS=

LIBC_OBJS=$(patsubst %.c,%.o,$(wildcard libc/*.c))

APPS_X=$(foreach app,$(APPS),base/bin/$(app))

all: image.iso

# Kernel

KCFLAGS  = -O2 -std=c99
KCFLAGS += -finline-functions -ffreestanding
KCFLAGS += -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format
KCFLAGS += -pedantic -fno-omit-frame-pointer
KCFLAGS += -D_KERNEL_
KCFLAGS += -DKERNEL_GIT_TAG=$(shell util/make-version)
KASFLAGS = --32

KERNEL_OBJS = $(patsubst %.c,%.o,$(wildcard kernel/*.c))
KERNEL_OBJS += $(patsubst %.c,%.o,$(wildcard kernel/*/*.c))
KERNEL_OBJS += $(patsubst %.c,%.o,$(wildcard kernel/*/*/*.c))

KERNEL_ASMOBJS = $(filter-out kernel/symbols.o,$(patsubst %.S,%.o,$(wildcard kernel/*.S)))

cdrom/kernel: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o
	${KCC} -T kernel/link.ld ${KCFLAGS} -nostdlib -o $@ ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o -lgcc

kernel/symbols.o: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} util/generate_symbols.py
	-rm -f kernel/symbols.o
	${KCC} -T kernel/link.ld ${KCFLAGS} -nostdlib -o .toaruos-kernel ${KERNEL_ASMOBJS} ${KERNEL_OBJS} -lgcc
	${KNM} .toaruos-kernel -g | util/generate_symbols.py > kernel/symbols.S
	${KAS} ${KASFLAGS} kernel/symbols.S -o $@
	-rm -f .toaruos-kernel

kernel/sys/version.o: kernel/*/*.c kernel/*.c

cdrom/mod:
	@mkdir -p $@

MODULES = $(patsubst modules/%.c,cdrom/mod/%.ko,$(wildcard modules/*.c))

HEADERS = $(shell find base/usr/include/kernel -type f -name '*.h')

cdrom/mod/%.ko: modules/%.c ${HEADERS} | cdrom/mod
	${KCC} -T modules/link.ld -nostdlib ${KCFLAGS} -c -o $@ $<

modules: ${MODULES}

kernel/%.o: kernel/%.S
	${KAS} ${ASFLAGS} $< -o $@

kernel/%.o: kernel/%.c ${HEADERS}
	${KCC} ${KCFLAGS} -nostdlib -g -c -o $@ $<

# Root Filesystem

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

# C Library

crts: base/lib/crt0.o base/lib/crti.o base/lib/crtn.o | dirs

base/lib/crt%.o: libc/crt%.s
	yasm -f elf -o $@ $<

libc/%.o: libc/%.c
	$(CC) -fPIC -c -m32 -Wa,--32 -O3 -o $@ $<

base/lib/libc.a: ${LIBC_OBJS} | dirs crts
	$(AR) cr $@ $^

base/lib/libc.so: ${LIBC_OBJS} | dirs crts
	$(CC) -nodefaultlibs -o $@ $(CFLAGS) -shared -fPIC $^ -lgcc

# Userspace Linker/Loader

base/lib/ld.so: linker/linker.c base/lib/libc.a | dirs
	$(CC) -static -Wl,-static $(CFLAGS) -o $@ -Os -T linker/link.ld $< $(LIBS)

# Shared Libraries

base/lib/libtoaru_graphics.so: lib/graphics.c base/usr/include/toaru/graphics.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_list.so: lib/list.c base/usr/include/toaru/list.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_tree.so: lib/tree.c base/usr/include/toaru/tree.h base/lib/libtoaru_list.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_list

base/lib/libtoaru_hashmap.so: lib/hashmap.c base/usr/include/toaru/hashmap.h base/lib/libtoaru_list.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_list

base/lib/libtoaru_kbd.so: lib/kbd.c base/usr/include/toaru/kbd.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_pthread.so: lib/pthread.c base/usr/include/toaru/pthread.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_pex.so: lib/pex.c base/usr/include/toaru/pex.h
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_dlfcn.so: lib/dlfcn.c
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $<

base/lib/libtoaru_yutani.so: lib/yutani.c base/usr/include/toaru/yutani.h base/lib/libtoaru_graphics.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_graphics

base/lib/libtoaru_rline.so: lib/rline.c base/usr/include/toaru/rline.h base/lib/libtoaru_kbd.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_kbd

base/lib/libtoaru_termemu.so: lib/termemu.c base/usr/include/toaru/termemu.h base/lib/libtoaru_graphics.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_graphics

base/lib/libtoaru_drawstring.so: lib/drawstring.c base/usr/include/toaru/drawstring.h base/lib/libtoaru_graphics.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_graphics

base/lib/libtoaru_decorations.so: lib/decorations.c base/usr/include/toaru/decorations.h base/lib/libtoaru_graphics.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_graphics

# Decoration Themes

base/lib/libtoaru-decor-fancy.so: decors/decor-fancy.c base/usr/include/toaru/decorations.h base/lib/libtoaru_graphics.so base/lib/libtoaru_decorations.so base/lib/libtoaru_drawstring.so
	$(CC) -o $@ $(CFLAGS) -shared -fPIC $< -ltoaru_decorations -ltoaru_drawstring -ltoaru_graphics

# Init

base/bin/init: apps/init.c base/lib/libc.a | dirs
	$(CC) -static -Wl,-static $(CFLAGS) -o $@ $< $(LIBS)

# Userspace

base/bin/sh: apps/sh.c base/lib/libc.so base/lib/libtoaru_list.so base/lib/libtoaru_rline.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_rline -ltoaru_list -ltoaru_kbd $(LIBS)

base/bin/migrate: apps/migrate.c base/lib/libc.so base/lib/libtoaru_list.so base/lib/libtoaru_hashmap.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/sysinfo: apps/sysinfo.c base/lib/libc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_termemu.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_graphics -ltoaru_termemu $(LIBS)

base/bin/terminal: apps/terminal.c base/lib/libc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_decorations.so base/lib/libtoaru_dlfcn.so base/lib/libtoaru_list.so base/lib/libtoaru_kbd.so base/lib/libtoaru_termemu.so base/lib/libtoaru_pex.so base/lib/libtoaru_hashmap.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_termemu -ltoaru_decorations -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_hashmap -ltoaru_dlfcn -ltoaru_kbd -ltoaru_list $(LIBS)

base/bin/terminal-vga: apps/terminal-vga.c base/lib/libc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_kbd.so base/lib/libtoaru_termemu.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_termemu -ltoaru_graphics -ltoaru_kbd $(LIBS)

base/bin/background: apps/background.c base/lib/libc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so base/lib/libtoaru_drawstring.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_drawstring -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/drawlines: apps/drawlines.c base/lib/libc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/yutani-query: apps/yutani-query.c base/lib/libc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/yutani-test: apps/yutani-test.c base/lib/libc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_yutani.so base/lib/libtoaru_pthread.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_graphics -ltoaru_pex -ltoaru_pthread -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/compositor: apps/compositor.c base/lib/libc.so base/lib/libtoaru_graphics.so base/lib/libtoaru_list.so base/lib/libtoaru_kbd.so base/lib/libtoaru_pthread.so base/lib/libtoaru_pex.so base/lib/libtoaru_yutani.so base/lib/libtoaru_hashmap.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_yutani -ltoaru_pthread -ltoaru_pex -ltoaru_graphics -ltoaru_kbd -ltoaru_hashmap -ltoaru_list $(LIBS)

base/bin/ls: apps/ls.c base/lib/libc.so base/lib/libtoaru_list.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_list $(LIBS)

base/bin/nyancat: apps/nyancat/nyancat.c base/lib/libc.so
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

base/bin/ps: apps/ps.c base/lib/libc.so base/lib/libtoaru_list.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_list $(LIBS)

base/bin/pstree: apps/pstree.c base/lib/libc.so base/lib/libtoaru_tree.so base/lib/libtoaru_list.so
	$(CC) $(CFLAGS) -o $@ $< -ltoaru_tree -ltoaru_list $(LIBS)

base/bin/%: apps/%.c base/lib/libc.so | dirs
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

# Ramdisk

cdrom/ramdisk.img: ${APPS_X} base/lib/ld.so base/lib/libtoaru-decor-fancy.so Makefile | dirs
	genext2fs -B 4096 -d base -U -b 4096 -N 2048 cdrom/ramdisk.img

# CD image

image.iso: cdrom/ramdisk.img cdrom/boot/boot.sys cdrom/kernel ${MODULES}
	xorriso -as mkisofs -R -J -c boot/bootcat -b boot/boot.sys -no-emul-boot -boot-load-size 20 -o image.iso cdrom

# Boot loader

cdrom/boot/boot.sys: boot/boot.o boot/cstuff.o boot/link.ld | cdrom/boot
	${KLD} -T boot/link.ld -o $@ boot/boot.o boot/cstuff.o

boot/cstuff.o: boot/cstuff.c boot/ata.h  boot/atapi_imp.h  boot/elf.h  boot/iso9660.h  boot/multiboot.h  boot/text.h  boot/types.h  boot/util.h
	${KCC} -c -Os -o $@ $<

boot/boot.o: boot/boot.s
	yasm -f elf -o $@ $<

.PHONY: clean
clean:
	rm -f base/lib/*.so
	rm -f base/lib/libc.a
	rm -f ${APPS_X}
	rm -f libc/*.o
	rm -f image.iso
	rm -f cdrom/ramdisk.img
	rm -f cdrom/boot/boot.sys
	rm -f boot/*.o
	rm -f cdrom/kernel
	rm -f ${KERNEL_OBJS} ${KERNEL_ASMOBJS}
	rm -f ${MODULES}

