# ToaruOS Build Scripts
ifneq ($(MAKECMDGOALS),toolchain)
 ifeq ($(TOOLCHAIN),)
  $(error $(shell util/helpful-toolchain-error.sh))
 else
  $(shell util/cache-toolchain.sh)
 endif
endif

KERNEL_TARGET=i686-elf
USER_TARGET=i686-pc-toaru

# Pretty output utilities.
BEG = util/mk-beg
END = util/mk-end
INFO = util/mk-info
ERRORS = 2>>/tmp/.`whoami`-build-errors || util/mk-error
ERRORSS = >>/tmp/.`whoami`-build-errors || util/mk-error
BEGRM = util/mk-beg-rm
ENDRM = util/mk-end-rm

# Rules start here.
.PHONY: all system install test toolchain userspace modules cdrom fix-cd
.PHONY: clean clean-soft clean-hard clean-user clean-mods clean-core clean-disk clean-once
.PHONY: run vga term headless curses quick
.PHONY: debug debug-vga debug-term debug-curses
.PHONY: virtualbox virtualbox-cdrom run-cdrom

# Prevents Make from removing intermediary files on failure
.SECONDARY:

# Disable built-in rules
.SUFFIXES:

all: $(shell util/detect-make-all.sh)

toolchain:
	@cd toolchain; ./toolchain-build.sh

###########################
# Emulator Pseudo-targets #
###########################

# Specify which modules should be included on startup.
# There are a few modules that are kinda required for a working system
# such as all of the dependencies needed to mount the root partition.
# We can also include things like the debug shell...
# Note that ordering matters - list dependencies first.
BOOT_MODULES := zero random serial
BOOT_MODULES += procfs tmpfs ata
#BOOT_MODULES += dospart
BOOT_MODULES += ext2
BOOT_MODULES += debug_shell
BOOT_MODULES += ps2mouse ps2kbd
BOOT_MODULES += lfbvideo
BOOT_MODULES += vidset
BOOT_MODULES += packetfs
BOOT_MODULES += snd
BOOT_MODULES += pcspkr
BOOT_MODULES += ac97
BOOT_MODULES += net rtl

# This is kinda silly. We're going to form an -initrd argument..
# which is basically -initrd "hdd/mod/%.ko,hdd/mod/%.ko..."
# for each of the modules listed above in BOOT_MODULES
COMMA := ,
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
BOOT_MODULES_X = -initrd "$(subst $(SPACE),$(COMMA),$(foreach mod,$(BOOT_MODULES),hdd/mod/$(mod).ko))"

# QEMU Configuration
EMU = qemu-system-i386

# Force the SDL backend with no frame and English (US) keyboard.
EMUARGS  = -sdl -no-frame -k en-us
# 1GB of RAM
EMUARGS += -m 1024
# Bochs VBE display device
EMUARGS += -vga std
# Realtime clock based on localtime (we don't NTP or support timezone configs yet)
EMUARGS += -rtc base=localtime
# Network hardware: RTL8139, usermode network emulation.
EMUARGS += -net nic,model=rtl8139 -net user
# Enable TCP dumps for monitoring.
EMUARGS += -net dump
# Sound hardware: Intel AC'97, PC beeper
EMUARGS += -soundhw pcspk,ac97
# Enable KVM if available, or fall back to TCG
EMUARGS += -M accel=kvm:tcg

# For development images, load the kernel, modules, hard disk.
EMUKARGS  = -kernel toaruos-kernel
EMUKARGS += $(BOOT_MODULES_X)
EMUKARGS += -hda toaruos-disk.img

# These arguments are passed to the kernel command line.
DISK_ROOT = root=/dev/hda
VID_QEMU  = vid=qemu,,1280,,720
START_VGA = start=--vga
START_SINGLE = start=--single
START_LIVE = start=live-welcome
WITH_LOGS = logtoserial=1

# Various different quick options
run: system
	${EMU} ${EMUARGS} ${EMUKARGS} -append "$(VID_QEMU) $(DISK_ROOT)"
quick: system
	${EMU} ${EMUARGS} ${EMUKARGS} -append "$(VID_QEMU) $(DISK_ROOT) start=quick-launch"
debug: system
	${EMU} ${EMUARGS} ${EMUKARGS} -serial stdio -append "$(VID_QEMU) $(WITH_LOGS) $(DISK_ROOT)"
vga: system
	${EMU} ${EMUARGS} ${EMUKARGS} -append "$(START_VGA) $(DISK_ROOT)"
debug-vga: system
	${EMU} ${EMUARGS} ${EMUKARGS} -serial stdio -append "$(WITH_LOGS) $(START_VGA) $(DISK_ROOT)"
term: system
	${EMU} ${EMUARGS} ${EMUKARGS} -append "$(VID_QEMU) $(START_SINGLE) $(DISK_ROOT)"
debug-term: system
	${EMU} ${EMUARGS} ${EMUKARGS} -serial stdio -append "$(VID_QEMU) $(START_SINGLE) $(WITH_LOGS) $(DISK_ROOT)"
headless: system
	${EMU} ${EMUARGS} ${EMUKARGS} -display none -append "$(START_VGA) $(DISK_ROOT)"
curses: system
	${EMU} ${EMUARGS} ${EMUKARGS} -curses -append "$(START_VGA) $(DISK_ROOT)"
debug-curses: system
	${EMU} ${EMUARGS} ${EMUKARGS} -serial file:serial-debug.log -curses -append "$(WITH_LOGS) $(START_VGA) $(DISK_ROOT)"
live: system
	${EMU} ${EMUARGS} ${EMUKARGS} -append "$(VID_QEMU) $(START_LIVE) $(DISK_ROOT)"

# Run the cdrom
run-cdrom: toaruos.iso
	${EMU} ${EMUARGS} -cdrom toaruos.iso

# Run VirtualBox
virtualbox: system
	util/run-virtualbox.sh
virtualbox-cdrom: toaruos.iso
	util/run-virtualbox-cdrom.sh

# Run the test suite
test: system
	expect util/test.exp

################
#    Kernel    #
################

# Kernel build flags
CFLAGS  = -O2 -std=c99
CFLAGS += -finline-functions -ffreestanding
CFLAGS += -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format
CFLAGS += -pedantic -fno-omit-frame-pointer
CFLAGS += -D_KERNEL_
ASFLAGS = --32

# Build kernel with bare elf toolchain
KCC = $(KERNEL_TARGET)-gcc
KNM = $(KERNEL_TARGET)-nm
KAR = $(KERNEL_TARGET)-ar
KAS = $(KERNEL_TARGET)-as
KLD = $(KERNEL_TARGET)-ld
KSTRIP = $(KERNEL_TARGET)-strip

# Kernel autoversioning with git sha
CFLAGS += -DKERNEL_GIT_TAG=`util/make-version`

# All of the core parts of the kernel are built directly.
KERNEL_OBJS = $(patsubst %.c,%.o,$(wildcard kernel/*.c))
KERNEL_OBJS += $(patsubst %.c,%.o,$(wildcard kernel/*/*.c))
KERNEL_OBJS += $(patsubst %.c,%.o,$(wildcard kernel/*/*/*.c))

KERNEL_ASMOBJS = $(filter-out kernel/symbols.o,$(patsubst %.S,%.o,$(wildcard kernel/*.S)))

toaruos-kernel: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o
	@${BEG} "CC" "$@"
	@${KCC} -T kernel/link.ld ${CFLAGS} -nostdlib -o toaruos-kernel ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o -lgcc ${ERRORS}
	@${END} "CC" "$@"
	@${INFO} "--" "Kernel is ready!"

kernel/symbols.o: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} util/generate_symbols.py
	@-rm -f kernel/symbols.o
	@${BEG} "NM" "Generating symbol list..."
	@${KCC} -T kernel/link.ld ${CFLAGS} -nostdlib -o toaruos-kernel ${KERNEL_ASMOBJS} ${KERNEL_OBJS} -lgcc ${ERRORS}
	@${KNM} toaruos-kernel -g | util/generate_symbols.py > kernel/symbols.S
	@${END} "NM" "Generated symbol list."
	@${BEG} "AS" "kernel/symbols.S"
	@${KAS} ${ASFLAGS} kernel/symbols.S -o $@ ${ERRORS}
	@${END} "AS" "kernel/symbols.S"

kernel/sys/version.o: kernel/*/*.c kernel/*.c

hdd/mod:
	@mkdir -p hdd/mod

# Loadable modules
MODULES = $(patsubst modules/%.c,hdd/mod/%.ko,$(wildcard modules/*.c))

# We also want to rebuild when a header changes.
# This is a naive approach, but it works...
HEADERS = $(shell find kernel/include/ -type f -name '*.h')

hdd/mod/%.ko: modules/%.c ${HEADERS} | hdd/mod
	@${BEG} "CC" "$< [module]"
	@${KCC} -T modules/link.ld -I./kernel/include -nostdlib ${CFLAGS} -c -o $@ $< ${ERRORS}
	@${END} "CC" "$< [module]"

modules: ${MODULES}

kernel/%.o: kernel/%.S
	@${BEG} "AS" "$<"
	@${KAS} ${ASFLAGS} $< -o $@ ${ERRORS}
	@${END} "AS" "$<"

kernel/%.o: kernel/%.c ${HEADERS}
	@${BEG} "CC" "$<"
	@${KCC} ${CFLAGS} -nostdlib -g -I./kernel/include -c -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

system: toaruos-disk.img toaruos-kernel ${MODULES}

#############
# Userspace #
#############

# Userspace build flags
USER_CFLAGS   = -nodefaultlibs -O3 -m32 -Wa,--32 -g -Iuserspace -isystem include -std=c99 -Lhdd/usr/lib
USER_BINFLAGS =
STRIP_LIBS = 1
HLIBS=-lnihc -lgcc

# We always build with our targetted cross-compiler
CC = $(USER_TARGET)-gcc
NM = $(USER_TARGET)-nm
AR = $(USER_TARGET)-ar
AS = $(USER_TARGET)-as
STRIP = $(USER_TARGET)-strip

LIBC_OBJS=$(patsubst %.c,%.o,$(wildcard libc/*.c))

# Userspace binaries and libraries
USER_CFILES   = $(filter-out userspace/apps/init.c,$(shell find userspace -not -wholename '*/lib/*' -not -wholename '*.static.*' -name '*.c'))
USER_LIBFILES = $(shell find userspace -wholename '*/lib/*' -name '*.c')

LIBC=hdd/usr/lib/libnihc.so

libc/%.o: libc/%.c
	@${BEG} "CC" $<
	@$(CC) -fPIC -c -m32 -Wa,--32 -O3 -isystem include -o $@ $<
	@${END} "CC" $<

# PYthon sources
PYTHON_LIBS = $(shell find userspace -wholename '*/lib/*' -name '*.py')
PYTHON_BINS = $(shell find userspace -wholename '*/bin/*' -name '*.py')

PYTHON_FILES  = $(foreach file,$(PYTHON_LIBS),$(patsubst %.py,hdd/usr/python/lib/python3.6/%.py,$(notdir ${file})))
PYTHON_FILES += $(foreach file,$(PYTHON_BINS),$(patsubst %.py,hdd/bin/%.py,$(notdir ${file})))

# Userspace output files (so we can define metatargets)
NONTEST_C   = $(foreach f,$(USER_CFILES),$(if $(findstring /tests/,$f),,$f))

NONTEST  = $(foreach file,$(NONTEST_C),$(patsubst %.c,hdd/bin/%,$(notdir ${file})))
NONTEST += $(foreach file,$(USER_LIBFILES),$(patsubst %.c,hdd/usr/lib/libtoaru-%.so,$(notdir ${file})))
NONTEST += $(LIBC) hdd/bin/init hdd/lib/ld.so

USERSPACE  = $(foreach file,$(USER_CFILES),$(patsubst %.c,hdd/bin/%,$(notdir ${file})))
USERSPACE += $(foreach file,$(USER_LIBFILES),$(patsubst %.c,hdd/usr/lib/libtoaru-%.so,$(notdir ${file})))
USERSPACE += $(LIBC) hdd/bin/init hdd/lib/ld.so

userspace: ${USERSPACE}

# Init must be built static at the moment.
hdd/bin/init: userspace/apps/init.c hdd/usr/lib/libnihc.a
	@${BEG} "CC" "$< (static)"
	@${CC} -o $@ -static -Wl,-static -nodefaultlibs -O3 -m32 -Wa,--32 -g -std=c99 -Iuserspace/apps -isystem include -Lhdd/usr/lib $< -lnihc -lgcc ${ERRORS}
	@${END} "CC" "$< (static)"

# Libraries
define user-c-rule
$1: $2 $(shell util/auto-dep.py --deps $2) $(LIBC)
	@${BEG} "CCSO" "$$<"
	@${CC} -o $$@ $(USER_CFLAGS) -shared -fPIC $$(shell util/auto-dep.py --cflags $$<) $$< $$(shell util/auto-dep.py --libs $$<) -lnihc -lgcc ${ERRORS}
	@if [ "x$(STRIP_LIBS)" = "x1" ]; then ${STRIP} $$@; fi
	@${END} "CCSO" "$$<"
endef
$(foreach file,$(USER_LIBFILES),$(eval $(call user-c-rule,$(patsubst %.c,hdd/usr/lib/libtoaru-%.so,$(notdir ${file})),${file})))

# Binaries from C sources
define user-c-rule
$1: $2 $(shell util/auto-dep.py --deps $2) $(LIBC)
	@${BEG} "CC" "$$<"
	@${CC} -o $$@ $(USER_CFLAGS) $(USER_BINFLAGS) -fPIE $$(shell util/auto-dep.py --cflags $$<) $$< $$(shell util/auto-dep.py --libs $$<) -lnihc -lgcc ${ERRORS}
	@${END} "CC" "$$<"
endef
$(foreach file,$(USER_CFILES),$(eval $(call user-c-rule,$(patsubst %.c,hdd/bin/%,$(notdir ${file})),${file})))

hdd/usr/lib:
	@mkdir -p hdd/usr/lib

hdd/lib:
	@mkdir -p hdd/lib

hdd/lib/ld.so: linker/linker.c hdd/usr/lib/libnihc.a | hdd/lib
	@${BEG} "CC" "$<"
	@${CC} -static -Wl,-static -std=c99 -isystem include -Lhdd/usr/lib -g -o $@ -Os -T linker/link.ld $< $(HLIBS) ${ERRORS}
	@${END} "CC" "$<"

hdd/usr/lib/libnihc.a: ${LIBC_OBJS} | hdd/usr/lib
	@${BEG} "AR" "$<"
	@${AR} cr $@ $^
	@${END} "AR" "$<"

hdd/usr/lib/libnihc.so: ${LIBC_OBJS} | hdd/usr/lib
	@${BEG} "CC" "$<"
	@${CC} -o $@ $(USER_CFLAGS) -shared -fPIC $^



# Python parts of userspace

hdd/usr/python/lib/python3.6:
	@mkdir -p $@

hdd/bin/%.py: userspace/py/bin/%.py
	@cp $< $@

hdd/usr/python/lib/python3.6/%.py: userspace/py/lib/%.py hdd/usr/python/lib/python3.6
	@cp $< $@

####################
# Hard Disk Images #
####################

# Hard disk image generation
GENEXT = genext2fs
DISK_SIZE = `util/disk_size.sh`

toaruos-disk.img: ${USERSPACE} util/devtable ${PYTHON_FILES}
	@${BEG} "hdd" "Generating a Hard Disk image..."
	@-rm -f toaruos-disk.img
	@${GENEXT} -B 4096 -d hdd -D util/devtable -U -b ${DISK_SIZE} -N 4096 toaruos-disk.img ${ERRORS}
	@${END} "hdd" "Generated Hard Disk image"
	@${INFO} "--" "Hard disk image is ready!"

########
# Boot #
########

#image.iso: stuff/boot/boot.sys
#»···xorriso -as mkisofs -R -J -c boot/bootcat -b boot/boot.sys -no-emul-boot -boot-load-size 20 -o image.iso stuff

boot/cstuff.o: boot/cstuff.c boot/ata.h  boot/atapi_imp.h  boot/elf.h  boot/iso9660.h  boot/multiboot.h  boot/text.h  boot/types.h  boot/util.h
	${KCC} -c -Os -o boot/cstuff.o boot/cstuff.c

boot/boot.o: boot/boot.s
	yasm -f elf -o $@ $<

_cdrom/boot:
	@mkdir -p _cdrom/boot

_cdrom/boot/boot.sys: boot/boot.o boot/cstuff.o boot/link.ld | _cdrom/boot
	${KLD} -T boot/link.ld -o $@ boot/boot.o boot/cstuff.o


#############
# CD Images #
#############

cdrom: toaruos.iso

hdd/usr/share/terminfo/t/toaru: util/toaru.tic
	@mkdir -p hdd/usr/share/terminfo/t
	@cp $< $@

FORCE:

_cdrom: FORCE | ${NONTEST} toaruos-kernel
	@-rm -rf _cdrom
	@mkdir _cdrom

_cdrom/mod: ${MODULES} _cdrom
	@mv hdd/mod $@

_cdrom/kernel: toaruos-kernel _cdrom
	@cp $< $@

_cdrom/ramdisk.img: ${NONTEST} util/devtable hdd/usr/share/terminfo/t/toaru _cdrom _cdrom/mod
	@${BEG} "ext" "Generating a ramdisk image..."
	@${STRIP} ${NONTEST}
	@${GENEXT} -B 4096 -d hdd -D util/devtable -U -b 3000 -N 2048 $@
	@${END} "ext" "Generated ramdisk image"

define fixup-cd
	@mv _cdrom/mod hdd/mod
	@rm -r _cdrom
endef

toaruos.iso: _cdrom/ramdisk.img _cdrom/kernel _cdrom/boot/boot.sys
	@${BEG} "ISO" "Building a CD image"
	@xorriso -as mkisofs -R -J -c boot/bootcat -b boot/boot.sys -no-emul-boot -boot-load-size 20 -o $@ _cdrom
	@${END} "ISO" "Building a CD image"
	$(call fixup-cd)
	@${INFO} "--" "CD generated"

fix-cd:
	$(call fixup-cd)


##############
#  packages  #
##############

kernel-headers.img.gz: kernel/include
	@${BEG} "pack" "$@"
	@rm -rf _kernel
	@mkdir _kernel
	@cp -rL kernel/include/* _kernel/
	@${GENEXT} -B 4096 -d _kernel -U -b 170 $@ ${ERRORS}
	@rm -r _kernel
	@${END} "pack" "$@"

##############
#    ctags   #
##############
tags: kernel/*/*.c kernel/*.c userspace/**/*.c modules/*.c linker/*.c
	@${BEG} "ctag" "Generating CTags..."
	@-ctags -R --c++-kinds=+p --fields=+iaS --extra=+q kernel userspace modules util linker ${ERRORS}
	@${END} "ctag" "Generated CTags."

###############
#    clean    #
###############

clean-soft:
	@${BEGRM} "RM" "Cleaning kernel objects..."
	@-rm -f kernel/*.o
	@-rm -f kernel/*/*.o
	@-rm -f ${KERNEL_OBJS}
	@${ENDRM} "RM" "Cleaned kernel objects"

clean-user:
	@${BEGRM} "RM" "Cleaning userspace products..."
	@-rm -f ${USERSPACE}
	@${ENDRM} "RM" "Cleaned userspace products"

clean-mods:
	@${BEGRM} "RM" "Cleaning kernel modules..."
	@-rm -f hdd/mod/*
	@${ENDRM} "RM" "Cleaned kernel modules"

clean-core:
	@${BEGRM} "RM" "Cleaning final output..."
	@-rm -f toaruos-kernel
	@${ENDRM} "RM" "Cleaned final output"

clean-disk:
	@${BEGRM} "RM" "Deleting hard disk image..."
	@-rm -f toaruos-disk.img
	@${ENDRM} "RM" "Deleted hard disk image"

clean: clean-soft clean-core
	@${INFO} "--" "Finished soft cleaning"
	@-rm -f ${LIBC_OBJS}
	@-rm -f hdd/usr/lib/*.so
	@-rm -f hdd/usr/lib/*.a
	@-rm -f hdd/lib/ld.so
	@-rm -f boot/*.o
	@-rm -f toaruos.iso

clean-hard: clean clean-user clean-mods
	@${INFO} "--" "Finished hard cleaning"


# vim:noexpandtab
# vim:tabstop=4
# vim:shiftwidth=4
