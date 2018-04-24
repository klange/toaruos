ifeq ($(TOOLCHAIN),)
  ifeq ($(shell util/check.sh),y)
    export PATH := $(shell util/activate.sh)
  else
    FOO := $(shell util/prompt.sh)
    ifeq ($(shell util/check.sh),y)
      export PATH := $(shell util/activate.sh)
    else
      $(error "No toolchain, and you did not ask to build it.")
    endif
  endif
endif

# Prevents Make from removing intermediary files on failure
.SECONDARY:

# Disable built-in rules
.SUFFIXES:

TARGET_TRIPLET=i686-pc-toaru

# Userspace flags

CC=$(TARGET_TRIPLET)-gcc
AR=$(TARGET_TRIPLET)-ar
CFLAGS= -O3 -m32 -Wa,--32 -g -std=c99 -I. -Iapps -pipe -mfpmath=sse -mmmx -msse -msse2 -fplan9-extensions

LIBC_OBJS=$(patsubst %.c,%.o,$(wildcard libc/*.c))
LC=base/lib/libc.so

APPS=$(patsubst apps/%.c,%,$(wildcard apps/*.c))
APPS_X=$(foreach app,$(APPS),base/bin/$(app))
APPS_Y=$(foreach app,$(filter-out init,$(APPS)),.make/$(app).mak)

LIBS=$(patsubst lib/%.c,%,$(wildcard lib/*.c))
LIBS_X=$(foreach lib,$(LIBS),base/lib/libtoaru_$(lib).so)
LIBS_Y=$(foreach lib,$(LIBS),.make/$(lib).lmak)

all: image.iso

# Kernel / module flags

KCC = $(TARGET_TRIPLET)-gcc
KAS = $(TARGET_TRIPLET)-as
KLD = $(TARGET_TRIPLET)-ld
KNM = $(TARGET_TRIPLET)-nm

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

# Kernel

cdrom/kernel: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o
	${KCC} -T kernel/link.ld ${KCFLAGS} -nostdlib -o $@ ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o -lgcc

kernel/symbols.o: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} util/generate_symbols.py
	-rm -f kernel/symbols.o
	${KCC} -T kernel/link.ld ${KCFLAGS} -nostdlib -o .toaruos-kernel ${KERNEL_ASMOBJS} ${KERNEL_OBJS} -lgcc
	${KNM} .toaruos-kernel -g | util/generate_symbols.py > kernel/symbols.S
	${KAS} ${KASFLAGS} kernel/symbols.S -o $@
	-rm -f .toaruos-kernel

kernel/sys/version.o: kernel/*/*.c kernel/*.c

kernel/%.o: kernel/%.S
	${KAS} ${ASFLAGS} $< -o $@

kernel/%.o: kernel/%.c ${HEADERS}
	${KCC} ${KCFLAGS} -nostdlib -g -c -o $@ $<

# Modules

cdrom/mod:
	@mkdir -p $@

MODULES = $(patsubst modules/%.c,cdrom/mod/%.ko,$(wildcard modules/*.c))

HEADERS = $(shell find base/usr/include/kernel -type f -name '*.h')

cdrom/mod/%.ko: modules/%.c ${HEADERS} | cdrom/mod
	${KCC} -T modules/link.ld -nostdlib ${KCFLAGS} -c -o $@ $<

modules: ${MODULES}

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
.make:
	mkdir -p .make
dirs: base/dev base/tmp base/proc base/bin base/lib cdrom/boot .make

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
	$(CC) -static -Wl,-static $(CFLAGS) -o $@ -Os -T linker/link.ld $<

# Shared Libraries
.make/%.lmak: lib/%.c util/auto-dep.py | dirs
	util/auto-dep.py --makelib $< > $@

ifeq (,$(findstring clean,$(MAKECMDGOALS)))
-include ${LIBS_Y}
endif

# Init (static)

base/bin/init: apps/init.c base/lib/libc.a | dirs
	$(CC) -static -Wl,-static $(CFLAGS) -o $@ $<

# Userspace applications

.make/%.mak: apps/%.c util/auto-dep.py | dirs
	util/auto-dep.py --make $< > $@

ifeq (,$(findstring clean,$(MAKECMDGOALS)))
-include ${APPS_Y}
endif

# Ramdisk

cdrom/ramdisk.img: ${APPS_X} ${LIBS_X} base/lib/ld.so Makefile | dirs
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
	rm -f ${KERNEL_OBJS} ${KERNEL_ASMOBJS} kernel/symbols.o kernel/symbols.S
	rm -f base/lib/crt*.o
	rm -f ${MODULES}
	rm -f ${APPS_Y} ${LIBS_Y}

