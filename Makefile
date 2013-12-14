# ToAruOS Primary Build Script
ifeq ($(CCC_ANALYZE),yes)
# CC is set by CCC_ANALYZE to clang
# It is not recommended that you use a kernel built this way
else
CC = i686-pc-toaru-gcc
endif
# CFLAGS for core components
CFLAGS = -Wall -Wextra -pedantic -m32 -O0 -std=c99 -finline-functions -fno-stack-protector -nostdinc -ffreestanding -Wno-unused-function -Wno-unused-parameter -Wstrict-prototypes -g
# Linker for core
LD = i686-pc-toaru-ld
YASM = yasm
# Feel free to be specific, but I'd rather you not be.
SUBMODULES  = $(patsubst %.c,%.o,$(wildcard kernel/*/*.c))

USERSPACE = $(shell find userspace/ -type f -name '*.c') $(shell find userspace/ -type f -name '*.cpp') $(shell find userspace/ -type f -name '*.h')

EMU = qemu-system-i386
GENEXT = genext2fs
DISK_SIZE = 524288
#DISK_SIZE = 131072
RAMDISK_SIZE = 32786
DD = dd conv=notrunc
BEG = util/mk-beg
END = util/mk-end
INFO = util/mk-info
ERRORS = 2>>/tmp/.`whoami`-build-errors || util/mk-error
ERRORSS = >>/tmp/.`whoami`-build-errors || util/mk-error

BEGRM = util/mk-beg-rm
ENDRM = util/mk-end-rm

EMUARGS     = -sdl -kernel toaruos-kernel -m 1024 -serial stdio -vga std -hda toaruos-disk.img -k en-us -no-frame -rtc base=localtime
EMUKVM      = -enable-kvm

.PHONY: all system clean clean-once clean-hard clean-soft clean-bin clean-aux clean-core install run
.SECONDARY: 

all: .passed system tags
system: .passed toaruos-disk.img toaruos-kernel

install: system
	@${BEG} "CP" "Installing to /boot..."
	@cp toaruos-kernel /boot/toaruos-kernel
	@cp toaruos-initrd /boot/toaruos-initrd
	@${END} "CP" "Installed to /boot"

# Various different quick options
run: system
	${EMU} ${EMUARGS} -append "vid=qemu hdd"
kvm: system
	${EMU} ${EMUARGS} ${EMUKVM} -append "vid=qemu hdd"
vga: system
	${EMU} ${EMUARGS} -append "vgaterm hdd"
vga-kvm: system
	${EMU} ${EMUARGS} ${EMUKVM} -append "vgaterm hdd"
term: system
	${EMU} ${EMUARGS} -append "vid=qemu single hdd"
term-kvm: system
	${EMU} ${EMUARGS} ${EMUKVM} -append "vid=qemu single hdd"
debug: system
	${EMU} ${EMUARGS} -append "logtoserial=0 vid=qemu hdd"
debug-term: system
	${EMU} ${EMUARGS} -append "logtoserial=0 vid=qemu single hdd"
debug-vga: system
	${EMU} ${EMUARGS} -append "logtoserial=0 vgaterm hdd"
headless: system
	${EMU} ${EMUARGS} -display none -append "vgaterm hdd"
run-config: system
	util/config-parser ${EMU}

test: system
	python util/run-tests.py 2>/dev/null

.passed:
	@util/check-reqs > /dev/null
	@touch .passed

################
#    Kernel    #
################
toaruos-kernel: kernel/start.o kernel/link.ld kernel/main.o ${SUBMODULES} .passed
	@${BEG} "LD" "$<"
	@${LD} -T kernel/link.ld -o toaruos-kernel kernel/*.o ${SUBMODULES} ${ERRORS}
	@${END} "LD" "$<"
	@${INFO} "--" "Kernel is ready!"

kernel/start.o: kernel/start.s
	@${BEG} "yasm" "$<"
	@${YASM} -f elf -o kernel/start.o kernel/start.s ${ERRORS}
	@${END} "yasm" "$<"

kernel/sys/version.o: kernel/*/*.c kernel/*.c

%.o: %.c
	@${BEG} "CC" "$<"
	@${CC} ${CFLAGS} -I./kernel/include -c -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

################
#   Ram disk   #
################
toaruos-initrd: .passed
	@${BEG} "initrd" "Generating initial RAM disk"
	@# Get rid of the old one
	@-rm -f toaruos-initrd
	@${GENEXT} -d initrd -q -b ${RAMDISK_SIZE} toaruos-initrd ${ERRORS}
	@${END} "initrd" "Generated initial RAM disk"
	@${INFO} "--" "Ramdisk image is ready!"

####################
# Hard Disk Images #
####################

# TODO: Install Grub to one of these by pulling newest grub builds
#       from the Grub2 website.

.userspace-check: ${USERSPACE}
	@cd userspace && python build.py
	@touch .userspace-check

toaruos-disk.img: .userspace-check
	@${BEG} "hdd" "Generating a Hard Disk image..."
	@-rm -f toaruos-disk.img
	@${GENEXT} -B 4096 -d hdd -U -b ${DISK_SIZE} -N 4096 toaruos-disk.img ${ERRORS}
	@${END} "hdd" "Generated Hard Disk image"
	@${INFO} "--" "Hard disk image is ready!"

##############
#    ctags   #
##############
tags: kernel/*/*.c kernel/*.c .userspace-check
	@${BEG} "ctag" "Generating CTags..."
	@ctags -R --c++-kinds=+p --fields=+iaS --extra=+q kernel userspace util
	@${END} "ctag" "Generated CTags."

###############
#    clean    #
###############

clean-soft:
	@${BEGRM} "RM" "Cleaning modules..."
	@-rm -f kernel/*.o
	@-rm -f ${SUBMODULES}
	@${ENDRM} "RM" "Cleaned modules."

clean-bin:
	@${BEGRM} "RM" "Cleaning native binaries..."
	@-rm -f hdd/bin/*
	@${ENDRM} "RM" "Cleaned native binaries"

clean-core:
	@${BEGRM} "RM" "Cleaning final output..."
	@-rm -f toaruos-kernel
	@-rm -f toaruos-initrd
	@${ENDRM} "RM" "Cleaned final output"

clean: clean-soft clean-core
	@${INFO} "--" "Finished soft cleaning"

clean-hard: clean clean-bin
	@${INFO} "--" "Finished hard cleaning"

clean-disk:
	@${BEGRM} "RM" "Deleting hard disk image..."
	@-rm -f toaruos-disk.img
	@${ENDRM} "RM" "Deleted hard disk image"

clean-once:
	@${BEGRM} "RM" "Cleaning one-time files..."
	@-rm -f .passed
	@${ENDRM} "RM" "Cleaned one-time files"

# vim:noexpandtab
# vim:tabstop=4
# vim:shiftwidth=4
