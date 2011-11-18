# ToAruOS Primary Build Script
# This script will pull either clang (with -fcolor-diagnostics), gcc (with no extra options), or cc
CC = `util/compiler`
# Sometimes we just have to use GCC
GCC = gcc
# CFLAGS for core components
CFLAGS = -Wall -Wextra -pedantic -m32 -O0 -std=c99 -finline-functions -fno-stack-protector -nostdinc -ffreestanding -Wno-unused-function -Wno-unused-parameter -g
# CFLAGS for native utils
NATIVEFLAGS = -std=c99 -g -pedantic -Wall -Wextra -Wno-unused-parameter
# Linker for core
LD = ld -m elf_i386
YASM = yasm
# Feel free to be specific, but I'd rather you not be.
MODULES = $(patsubst %.c,%.o,$(wildcard kernel/core/*.c))
FILESYSTEMS = $(patsubst %.c,%.o,$(wildcard kernel/core/fs/*.c))
VIDEODRIVERS = $(patsubst %.c,%.o,$(wildcard kernel/core/video/*.c))
BINARIES = initrd/bin/hello initrd/bin/echo initrd/bin/yes initrd/bin/cat initrd/bin/sh initrd/bin/clear
UTILITIES = util/bin/readelf util/bin/typewriter
EMU = qemu
GENEXT = genext2fs
DD = dd conv=notrunc
BEG = util/mk-beg
END = util/mk-end
INFO = util/mk-info
ERRORS = 2>>/tmp/.build-errors || util/mk-error
ERRORSS = >>/tmp/.build-errors || util/mk-error

BEGRM = util/mk-beg-rm
ENDRM = util/mk-end-rm

EMUARGS = -kernel toaruos-kernel -initrd toaruos-initrd -append "vid=qemu wallpaper=/usr/share/wallpaper.bmp" -serial stdio -vga std -hda toaruos-disk.img
EMUKVM  = -enable-kvm

.PHONY: all check-toolchain system clean clean-once clean-hard clean-soft clean-docs clean-bin clean-aux clean-core clean-boot install run docs utils
.SECONDARY: 

all: .passed system bootdisk.img docs utils
system: toaruos-initrd toaruos-disk.img toaruos-kernel

install: system
	@${BEG} "CP" "Installing to /boot..."
	@cp toaruos-kernel /boot/toaruos-kernel
	@cp toaruos-initrd /boot/toaruos-initrd
	@${END} "CP" "Installed to /boot"

run: system
	${EMU} ${EMUARGS}

kvm: system
	${EMU} ${EMUARGS} ${EMUKVM}

utils: ${UTILITIES}

.passed:
	@util/check-reqs > /dev/null
	@touch .passed

check-toolchain:
	@util/install-toolchain.sh

#################
# Documentation #
#################
docs: docs/core.pdf

docs/core.pdf: docs/*.tex 
	@${BEG} "docs" "Generating documentation..."
	@pdflatex -draftmode -halt-on-error -output-directory docs/ docs/core.tex > /dev/null ${ERRORS}
	@makeindex -q docs/*.idx ${ERRORS}
	@pdflatex -halt-on-error -output-directory docs/ docs/core.tex > /dev/null ${ERRORS}
	@${END} "docs" "Generated documentation"

################
#    Kernel    #
################
toaruos-kernel: kernel/start.o kernel/link.ld kernel/main.o ${MODULES} ${FILESYSTEMS} ${VIDEODRIVERS}
	@${BEG} "LD" "$<"
	@${LD} -T kernel/link.ld -o toaruos-kernel kernel/*.o kernel/core/*.o kernel/core/fs/*.o kernel/core/video/*.o ${ERRORS}
	@${END} "LD" "$<"
	@${INFO} "--" "Kernel is ready!"

kernel/start.o: kernel/start.s
	@${BEG} "yasm" "$<"
	@${YASM} -f elf -o kernel/start.o kernel/start.s ${ERRORS}
	@${END} "yasm" "$<"

%.o: %.c
	@${BEG} "CC" "$<"
	@${CC} ${CFLAGS} -I./kernel/include -c -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

################
#   Ram disk   #
################
toaruos-initrd: initrd/boot/kernel initrd/boot/stage2 initrd/bs.bmp ${BINARIES}
	@${BEG} "initrd" "Generating initial RAM disk"
	@# Get rid of the old one
	@-rm -f toaruos-initrd
	@${GENEXT} -d initrd -q -b 4096 toaruos-initrd ${ERRORS}
	@${END} "initrd" "Generated initial RAM disk"
	@${INFO} "--" "Ramdisk image is ready!"

### Ram Disk installers...

# Second stage bootloader
initrd/boot/stage2: bootloader/stage2.bin
	@mkdir -p initrd/boot
	@cp bootloader/stage2.bin initrd/boot/stage2

# Kernel
initrd/boot/kernel: toaruos-kernel
	@mkdir -p initrd/boot
	@cp toaruos-kernel initrd/boot/kernel

####################
# Hard Disk Images #
####################

# TODO: Install Grub to one of these by pulling newest grub builds
#       from the Grub2 website.

hdd:
	@mkdir hdd

toaruos-disk.img: hdd
	@${BEG} "hdd" "Generating a Hard Disk image..."
	@-rm -f toaruos-disk.img
	@${GENEXT} -d hdd -q -b 131072 -N 4096 toaruos-disk.img ${ERRORS}
	@${END} "hdd" "Generated Hard Disk image"
	@${INFO} "--" "Hard disk image is ready!"

################
#   Utilities  #
################

util/bin/%: util/%.c
	@${BEG} "CC" "$<"
	@${CC} ${NATIVEFLAGS} -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

################
#   Userspace  #
################
loader/crtbegin.o: loader/crtbegin.s
	@${BEG} "yasm" "$<"
	@${YASM} -f elf32 -o $@ $< ${ERRORS}
	@${END} "yasm" "$<"

initrd/bin/%: loader/%.o loader/crtbegin.o loader/syscall.o
	@${BEG} "LD" "$<"
	@${LD} -T loader/link.ld -s -S -o $@ $< ${ERRORS}
	@${END} "LD" "$<"

loader/%.o: loader/%.c
	@${BEG} "CC" "$<"
	@${CC} ${CFLAGS} -c -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

################
#  Bootloader  #
################

# Stage 1
bootloader/stage1/main.o: bootloader/stage1/main.c
	@${BEG} "CC" "$<"
	@${GCC} ${CFLAGS} -c -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

bootloader/stage1/start.o: bootloader/stage1/start.s
	@${BEG} "yasm" "$<"
	@${YASM} -f elf32 -p gas -o $@ $< ${ERRORS}
	@${END} "yasm" "$<"

bootloader/stage1.bin: bootloader/stage1/main.o bootloader/stage1/start.o bootloader/stage1/link.ld
	@${BEG} "LD" "$<"
	@${LD} -o bootloader/stage1.bin -T bootloader/stage1/link.ld bootloader/stage1/start.o bootloader/stage1/main.o ${ERRORS}
	@${END} "LD" "$<"

# Stage 2
bootloader/stage2/main.o: bootloader/stage2/main.c
	@${BEG} "CC" "$<"
	@${GCC} ${CFLAGS} -I./kernel/include -c -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

bootloader/stage2/start.o: bootloader/stage2/start.s
	@${BEG} "yasm" "$<"
	@${YASM} -f elf32 -p gas -o $@ $< ${ERRORS}
	@${END} "yasm" "$<"

bootloader/stage2.bin: bootloader/stage2/main.o bootloader/stage2/start.o bootloader/stage2/link.ld
	@${BEG} "LD" "$<"
	@${LD} -o bootloader/stage2.bin -T bootloader/stage2/link.ld bootloader/stage2/start.o bootloader/stage2/main.o ${ERRORS}
	@${END} "LD" "$<"

##############
#  bootdisk  #
##############
bootdisk.img: bootloader/stage1.bin bootloader/stage2.bin util/bin/mrboots-installer
	@cat bootloader/stage1.bin bootloader/stage2.bin > bootdisk.img
	@${INFO} "--" "Bootdisk is ready!"

###############
#    clean    #
###############

clean-soft:
	@${BEGRM} "RM" "Cleaning modules..."
	@-rm -f kernel/*.o
	@-rm -f kernel/core/*.o
	@-rm -f kernel/core/fs/*.o
	@-rm -f kernel/core/video/*.o
	@${ENDRM} "RM" "Cleaned modules."

clean-docs:
	@${BEGRM} "RM" "Cleaning documentation..."
	@-rm -f docs/*.pdf docs/*.aux docs/*.log docs/*.out
	@-rm -f docs/*.idx docs/*.ind docs/*.toc docs/*.ilg
	@${ENDRM} "RM" "Cleaned documentation"

clean-boot:
	@${BEGRM} "RM" "Cleaning bootloader..."
	@-rm -f bootloader/stage1.bin
	@-rm -f bootloader/stage1/*.o
	@-rm -f bootloader/stage2.bin
	@-rm -f bootloader/stage2/*.o
	@-rm -f -r initrd/boot
	@-rm -f bootdisk.img
	@${ENDRM} "RM" "Cleaned bootloader"

clean-bin:
	@${BEGRM} "RM" "Cleaning native binaries..."
	@-rm -f initrd/bin/*
	@${ENDRM} "RM" "Cleaned native binaries"

clean-aux:
	@${BEGRM} "RM" "Cleaning auxillary files..."
	@-rm -f loader/*.o
	@-rm -f util/bin/*
	@${ENDRM} "RM" "Cleaned auxillary files"

clean-core:
	@${BEGRM} "RM" "Cleaning final output..."
	@-rm -f toaruos-kernel
	@-rm -f toaruos-initrd
	@${ENDRM} "RM" "Cleaned final output"

clean: clean-soft clean-boot clean-core
	@${INFO} "--" "Finished soft cleaning"

clean-hard: clean clean-bin clean-aux clean-docs
	@${INFO} "--" "Finished hard cleaning"

clean-once:
	@${BEGRM} "RM" "Cleaning one-time files..."
	@-rm -f .passed
	@-rm -f toaruos-disk.img
	@${ENDRM} "RM" "Cleaned one-time files"


# vim:noexpandtab
# vim:tabstop=4
# vim:shiftwidth=4
