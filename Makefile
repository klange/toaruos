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
FILESYSTEMS  = $(patsubst %.c,%.o,$(wildcard kernel/fs/*.c))
VIDEODRIVERS = $(patsubst %.c,%.o,$(wildcard kernel/video/*.c))
DEVICES      = $(patsubst %.c,%.o,$(wildcard kernel/devices/*.c))
VIRTUALMEM   = $(patsubst %.c,%.o,$(wildcard kernel/mem/*.c))
MISCMODS     = $(patsubst %.c,%.o,$(wildcard kernel/misc/*.c))
SYSTEM       = $(patsubst %.c,%.o,$(wildcard kernel/sys/*.c))
DATASTRUCTS  = $(patsubst %.c,%.o,$(wildcard kernel/ds/*.c))
CPUBITS      = $(patsubst %.c,%.o,$(wildcard kernel/cpu/*.c))
REALEMU      = $(patsubst %.c,%.o,$(wildcard kernel/v8086/*.c))

SUBMODULES = ${MODULES} ${FILESYSTEMS} ${VIDEODRIVERS} ${DEVICES} ${VIRTUALMEM} ${MISCMODS} ${SYSTEM} ${DATASTRUCTS} ${CPUBITS} ${REALEMU}

UTILITIES = util/bin/readelf util/bin/typewriter
EMU = qemu
GENEXT = genext2fs
DD = dd conv=notrunc
BEG = util/mk-beg
END = util/mk-end
INFO = util/mk-info
ERRORS = 2>>/tmp/.`whoami`-build-errors || util/mk-error
ERRORSS = >>/tmp/.`whoami`-build-errors || util/mk-error

BEGRM = util/mk-beg-rm
ENDRM = util/mk-end-rm

EMUARGS = -kernel toaruos-kernel -m 256 -initrd toaruos-initrd -append "vid=qemu hdd" -serial stdio -vga std -hda toaruos-disk.img -k en-us -no-frame
EMUKVM  = -enable-kvm

.PHONY: all system clean clean-once clean-hard clean-soft clean-docs clean-bin clean-aux clean-core update-version install run docs utils
.SECONDARY: 

all: .passed system docs utils tags
system: .passed toaruos-initrd toaruos-disk.img toaruos-kernel

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
toaruos-initrd: .passed initrd/boot/kernel
	@${BEG} "initrd" "Generating initial RAM disk"
	@# Get rid of the old one
	@-rm -f toaruos-initrd
	@${GENEXT} -d initrd -q -b 4096 toaruos-initrd ${ERRORS}
	@${END} "initrd" "Generated initial RAM disk"
	@${INFO} "--" "Ramdisk image is ready!"

### Ram Disk installers...

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

toaruos-disk.img: hdd userspace/*.c
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

hdd/bin/%: loader/%.o loader/crtbegin.o loader/syscall.o
	@${BEG} "LD" "$<"
	@${LD} -T loader/link.ld -s -S -o $@ $< ${ERRORS}
	@${END} "LD" "$<"

##############
#    ctags   #
##############
tags: kernel/*/*.c kernel/*.c userspace/*.c
	@${BEG} "ctag" "Generating CTags..."
	@ctags -R --c++-kinds=+p --fields=+iaS --extra=+q
	@${END} "ctag" "Generated CTags."

###############
#    clean    #
###############

clean-soft:
	@${BEGRM} "RM" "Cleaning modules..."
	@-rm -f kernel/*.o
	@-rm -f ${SUBMODULES}
	@${ENDRM} "RM" "Cleaned modules."

clean-docs:
	@${BEGRM} "RM" "Cleaning documentation..."
	@-rm -f docs/*.pdf docs/*.aux docs/*.log docs/*.out
	@-rm -f docs/*.idx docs/*.ind docs/*.toc docs/*.ilg
	@${ENDRM} "RM" "Cleaned documentation"

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

clean: clean-soft clean-core
	@${INFO} "--" "Finished soft cleaning"

clean-hard: clean clean-bin clean-aux clean-docs
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
