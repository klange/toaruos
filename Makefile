TOOLCHAIN=util
BASE=base
export PATH := $(shell $(TOOLCHAIN)/activate.sh)

include build/x86_64.mk

CC = ${TARGET}-gcc
NM = ${TARGET}-nm
CXX= ${TARGET}-g++
AR = ${TARGET}-ar
AS = ${TARGET}-as
OC = ${TARGET}-objcopy

KERNEL_CFLAGS  = -ffreestanding -O2 -std=gnu11 -g -static
KERNEL_CFLAGS += -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
KERNEL_CFLAGS += -pedantic -Wwrite-strings ${ARCH_KERNEL_CFLAGS}

# Defined constants for the kernel
KERNEL_CFLAGS += -D_KERNEL_ -DKERNEL_ARCH=${ARCH}
KERNEL_CFLAGS += -DKERNEL_GIT_TAG=`util/make-version`

KERNEL_OBJS =  $(patsubst %.c,%.o,$(wildcard kernel/*.c))
KERNEL_OBJS += $(patsubst %.c,%.o,$(wildcard kernel/*/*.c))
KERNEL_OBJS += $(patsubst %.c,%.o,$(wildcard kernel/arch/${ARCH}/*.c))

KERNEL_ASMOBJS  = $(filter-out kernel/symbols.o,$(patsubst %.S,%.o,$(wildcard kernel/arch/${ARCH}/*.S)))

KERNEL_SOURCES  = $(wildcard kernel/*.c) $(wildcard kernel/*/*.c) $(wildcard kernel/${ARCH}/*/*.c)
KERNEL_SOURCES += $(wildcard kernel/arch/${ARCH}/*.S)

MODULES = $(patsubst %.c,%.ko,$(wildcard modules/*.c))

# Configs you can override.
SMP ?= 1
RAM ?= 3G
EXTRA_ARGS ?=

EMU = qemu-system-x86_64
EMU_ARGS  = -kernel misaka-kernel
EMU_ARGS += -M q35
EMU_ARGS += -m $(RAM)
EMU_ARGS += -smp $(SMP)
EMU_ARGS += -no-reboot
#EMU_ARGS += -display none
EMU_ARGS += -serial mon:stdio
EMU_ARGS += -rtc base=localtime
EMU_ARGS += -soundhw pcspk,ac97
EMU_ARGS += -netdev user,id=u1,hostfwd=tcp::5555-:23 -device e1000e,netdev=u1 -object filter-dump,id=f1,netdev=u1,file=qemu-e1000e.pcap
EMU_ARGS += -netdev user,id=u2,hostfwd=tcp::5580-:80 -device e1000,netdev=u2 -object filter-dump,id=f2,netdev=u2,file=qemu.pcap
#EMU_ARGS += -hda toaruos-disk.img
EMU_KVM  ?= -enable-kvm

APPS=$(patsubst apps/%.c,%,$(wildcard apps/*.c)) $(patsubst apps/%.c++,%,$(wildcard apps/*.c++))
APPS_X=$(foreach app,$(APPS),$(BASE)/bin/$(app))
APPS_Y=$(foreach app,$(APPS),.make/$(app).mak)
APPS_SH=$(patsubst apps/%.sh,%.sh,$(wildcard apps/*.sh))
APPS_SH_X=$(foreach app,$(APPS_SH),$(BASE)/bin/$(app))
APPS_KRK=$(patsubst apps/%.krk,%.krk,$(wildcard apps/*.krk))
APPS_KRK_X=$(foreach app,$(APPS_KRK),$(BASE)/bin/$(app))

LIBS=$(patsubst lib/%.c,%,$(wildcard lib/*.c))
LIBS_X=$(foreach lib,$(LIBS),$(BASE)/lib/libtoaru_$(lib).so)
LIBS_Y=$(foreach lib,$(LIBS),.make/$(lib).lmak)

KRK_MODS = $(patsubst kuroko/src/modules/module_%.c,$(BASE)/lib/kuroko/%.so,$(wildcard kuroko/src/modules/module_*.c))
KRK_MODS_X = $(patsubst lib/kuroko/%.c,$(BASE)/lib/kuroko/%.so,$(wildcard lib/kuroko/*.c))
KRK_MODS_Y = $(patsubst lib/kuroko/%.c,.make/%.kmak,$(wildcard lib/kuroko/*.c))

CFLAGS= -O2 -std=gnu11 -I. -Iapps -fplan9-extensions -Wall -Wextra -Wno-unused-parameter

LIBC_OBJS  = $(patsubst %.c,%.o,$(wildcard libc/*.c))
LIBC_OBJS += $(patsubst %.c,%.o,$(wildcard libc/*/*.c))
LIBC_OBJS += $(patsubst %.c,%.o,$(wildcard libc/arch/${ARCH}/*.c))

GCC_SHARED = $(BASE)/usr/lib/libgcc_s.so.1 $(BASE)/usr/lib/libgcc_s.so
LIBSTDCXX =  $(BASE)/usr/lib/libstdc++.so.6.0.28 $(BASE)/usr/lib/libstdc++.so.6 $(BASE)/usr/lib/libstdc++.so

CRTS  = $(BASE)/lib/crt0.o $(BASE)/lib/crti.o $(BASE)/lib/crtn.o

LC = $(BASE)/lib/libc.so $(GCC_SHARED) $(LIBSTDCXX)

.PHONY: all system clean run shell

all: system
system: misaka-kernel $(MODULES) ramdisk.igz

%.ko: %.c
	${CC} -c ${KERNEL_CFLAGS} -o $@ $<

ramdisk.igz: $(wildcard $(BASE)/* $(BASE)/*/* $(BASE)/*/*/*) $(APPS_X) $(LIBS_X) $(KRK_MODS_X) $(BASE)/bin/kuroko $(BASE)/lib/ld.so $(APPS_KRK_X) $(KRK_MODS)
	python3 util/createramdisk.py

KRK_SRC = $(sort $(wildcard kuroko/src/*.c))
$(BASE)/bin/kuroko: $(KRK_SRC) $(CRTS) | $(LC)
	$(CC) -O2 -g -o $@ -Wl,--export-dynamic -Ikuroko/src $(KRK_SRC) kuroko/src/vendor/rline.c

$(BASE)/lib/kuroko/%.so: kuroko/src/modules/module_%.c| dirs $(LC)
	$(CC) -O2 -shared -fPIC -Ikuroko/src -o $@ $<

$(BASE)/lib/libkuroko.so: $(KRK_SRC) | $(LC)
	$(CC) -O2 -shared -fPIC -Ikuroko/src -o $@ $(filter-out kuroko/src/kuroko.c,$(KRK_SRC))

$(BASE)/lib/ld.so: linker/linker.c $(BASE)/lib/libc.a | dirs $(LC)
	$(CC) -g -static -Wl,-static $(CFLAGS) -o $@ -Os -T linker/link.ld $<

run: system
	${EMU} ${EMU_ARGS} ${EMU_KVM} -append "root=/dev/ram0 start=live-session migrate $(EXTRA_ARGS)" -initrd ramdisk.igz

shell: system
	${EMU} -m $(RAM) ${EMU_KVM} -kernel misaka-kernel -append "root=/dev/ram0 start=--headless migrate" -initrd ramdisk.igz \
		-nographic -no-reboot -audiodev none,id=id -serial null -serial mon:stdio \
		-fw_cfg name=opt/org.toaruos.gettyargs,string="-a local /dev/ttyS1" \
		-fw_cfg name=opt/org.toaruos.term,string=${TERM}

misaka-kernel: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o
	${CC} -g -T kernel/arch/${ARCH}/link.ld ${KERNEL_CFLAGS} -o $@.64 ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o -lgcc
	${OC} -I elf64-x86-64 -O elf32-i386 $@.64 $@

kernel/sys/version.o: ${KERNEL_SOURCES}

kernel/symbols.o: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} util/gensym.krk
	-rm -f kernel/symbols.o
	${CC} -T kernel/arch/${ARCH}/link.ld ${KERNEL_CFLAGS} -o misaka-kernel.64 ${KERNEL_ASMOBJS} ${KERNEL_OBJS} -lgcc
	${NM} misaka-kernel.64 -g | kuroko util/gensym.krk > kernel/symbols.S
	${CC} -c kernel/symbols.S -o $@

kernel/%.o: kernel/%.S
	echo ${PATH}
	${CC} -c $< -o $@

HEADERS = $(wildcard base/usr/include/kernel/*.h)

kernel/%.o: kernel/%.c ${HEADERS}
	${CC} ${KERNEL_CFLAGS} -nostdlib -g -Iinclude -c -o $@ $<

clean:
	-rm -f ${KERNEL_ASMOBJS}
	-rm -f ${KERNEL_OBJS}
	-rm -f kernel/symbols.o misaka-kernel misaka-kernel.64
	-rm -f ramdisk.tar ramdisk.igz 
	-rm -f $(APPS_Y) $(LIBS_Y) $(KRK_MODS_Y) $(KRK_MODS)
	-rm -f $(APPS_X) $(LIBS_X) $(KRK_MODS_X) $(APPS_KRK_X) $(APPS_SH_X)
	-rm -f $(BASE)/lib/crt0.o $(BASE)/lib/crti.o $(BASE)/lib/crtn.o
	-rm -f $(BASE)/lib/libc.so $(BASE)/lib/libc.a
	-rm -f $(LIBC_OBJS) $(BASE)/lib/ld.so $(BASE)/lib/libkuroko.so $(BASE)/lib/libm.so
	-rm -f $(BASE)/bin/kuroko
	-rm -f $(GCC_SHARED) $(LIBSTDCXX)

libc/%.o: libc/%.c base/usr/include/syscall.h 
	$(CC) -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fPIC -c -o $@ $<

.PHONY: libc
libc: $(BASE)/lib/libc.a $(BASE)/lib/libc.so

$(BASE)/lib/libc.a: ${LIBC_OBJS} $(CRTS)
	$(AR) cr $@ $(LIBC_OBJS)

$(BASE)/lib/libc.so: ${LIBC_OBJS} | $(CRTS)
	${CC} -nodefaultlibs -shared -fPIC -o $@ $^

$(BASE)/lib/crt%.o: libc/arch/${ARCH}/crt%.S
	${AS} -o $@ $<

$(BASE)/usr/lib/%: util/local/${TARGET}/lib/% | dirs
	cp -a $< $@
	-strip $@

$(BASE)/lib/libm.so: util/libm.c
	$(CC) -shared -nostdlib -fPIC -o $@ $<

$(BASE)/dev:
	mkdir -p $@
$(BASE)/tmp:
	mkdir -p $@
$(BASE)/proc:
	mkdir -p $@
$(BASE)/bin:
	mkdir -p $@
$(BASE)/lib:
	mkdir -p $@
$(BASE)/cdrom:
	mkdir -p $@
$(BASE)/var:
	mkdir -p $@
$(BASE)/lib/kuroko:
	mkdir -p $@
$(BASE)/usr/lib:
	mkdir -p $@
fatbase/efi/boot:
	mkdir -p $@
cdrom:
	mkdir -p $@
.make:
	mkdir -p .make
dirs: $(BASE)/dev $(BASE)/tmp $(BASE)/proc $(BASE)/bin $(BASE)/lib $(BASE)/cdrom $(BASE)/usr/lib $(BASE)/lib/kuroko cdrom $(BASE)/var fatbase/efi/boot .make

ifeq (,$(findstring clean,$(MAKECMDGOALS)))
-include ${APPS_Y}
-include ${LIBS_Y}
-include ${KRK_MODS_Y}
endif

.make/%.lmak: lib/%.c util/auto-dep.krk | dirs $(CRTS)
	kuroko util/auto-dep.krk --makelib $< > $@

.make/%.mak: apps/%.c util/auto-dep.krk | dirs $(CRTS)
	kuroko util/auto-dep.krk --make $< > $@

.make/%.mak: apps/%.c++ util/auto-dep.krk | dirs $(CRTS)
	kuroko util/auto-dep.krk --make $< > $@

.make/%.kmak: lib/kuroko/%.c util/auto-dep.krk | dirs
	kuroko util/auto-dep.krk --makekurokomod $< > $@

$(BASE)/bin/%.sh: apps/%.sh
	cp $< $@
	chmod +x $@

$(BASE)/bin/%.krk: apps/%.krk
	cp $< $@
	chmod +x $@

.PHONY: libs
libs: $(LIBS_X)

.PHONY: apps
apps: $(APPS_X)

.PHONY: libstdcxx
libstdcxx: $(LIBSTDCXX)

util/local/${TARGET}/lib/libstdc++.so.6.0.28: | $(BASE)/lib/libm.so
	cd util/build/gcc && make all-target-libstdc++-v3 && make install-target-libstdc++-v3

SOURCE_FILES  = $(wildcard kernel/*.c kernel/*/*.c kernel/*/*/*.c kernel/*/*/*/*.c)
SOURCE_FILES += $(wildcard apps/*.c linker/*.c libc/*.c libc/*/*.c lib/*.c lib/kuroko/*.c)
SOURCE_FILES += $(wildcard kuroko/src/*.c kuroko/src/*.h kuroko/src/*/*.c kuroko/src/*/*.h)
SOURCE_FILES += $(wildcard $(BASE)/usr/include/*.h $(BASE)/usr/include/*/*.h $(BASE)/usr/include/*/*/*.h)
tags: $(SOURCE_FILES)
	ctags -f tags $(SOURCE_FILES)
