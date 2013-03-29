# ToAruOS Primary Build Script
# This script will pull either clang (with -fcolor-diagnostics), gcc (with no extra options), or cc
#
ifeq ($(CCC_ANALYZE),yes)
else
CC = `util/compiler`
endif
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

SUBMODULES = ${MODULES} ${FILESYSTEMS} ${VIDEODRIVERS} ${DEVICES} ${VIRTUALMEM} ${MISCMODS} ${SYSTEM} ${DATASTRUCTS} ${CPUBITS}
USERSPACE = $(shell find userspace/ -type f -name '*.c') $(shell find userspace/ -type f -name '*.cpp') $(shell find userspace/ -type f -name '*.h')

UTILITIES = util/bin/readelf util/bin/typewriter util/bin/bim
EMU = qemu-system-i386
GENEXT = genext2fs
DISK_SIZE = 262144
DD = dd conv=notrunc
BEG = util/mk-beg
END = util/mk-end
INFO = util/mk-info
ERRORS = 2>>/tmp/.`whoami`-build-errors || util/mk-error
ERRORSS = >>/tmp/.`whoami`-build-errors || util/mk-error

BEGRM = util/mk-beg-rm
ENDRM = util/mk-end-rm

EMUARGS     = -kernel toaruos-kernel -m 1024 -serial stdio -vga std -hda toaruos-disk.img -k en-us -no-frame -rtc base=localtime
EMUKVM      = -enable-kvm

.PHONY: all system clean clean-once clean-hard clean-soft clean-docs clean-bin clean-aux clean-core install run docs utils
.SECONDARY: 

all: .passed system tags
aux: utils docs
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
run-config: system
	util/config-parser | xargs ${EMU}

test: system
	python util/run-tests.py 2>/dev/null

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
toaruos-initrd: .passed
	@${BEG} "initrd" "Generating initial RAM disk"
	@# Get rid of the old one
	@-rm -f toaruos-initrd
	@#${GENEXT} -d initrd -q -b 20480 toaruos-initrd ${ERRORS}
	@#${GENEXT} -d initrd -q -b 8192 toaruos-initrd ${ERRORS}
	@${GENEXT} -d hdd -q -b 81920 toaruos-initrd ${ERRORS}
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
	@${GENEXT} -d hdd -q -b ${DISK_SIZE} -N 4096 toaruos-disk.img ${ERRORS}
	@${END} "hdd" "Generated Hard Disk image"
	@${INFO} "--" "Hard disk image is ready!"

################
#   Utilities  #
################

util/bin/bim: userspace/bim.c
	@${BEG} "CC" "$<"
	@${CC} -std=c99 -posix -m32 -g -o $@ $< userspace/lib/wcwidth.c
	@${END} "CC" "$<"

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

clean-docs:
	@${BEGRM} "RM" "Cleaning documentation..."
	@-rm -f docs/*.pdf docs/*.aux docs/*.log docs/*.out
	@-rm -f docs/*.idx docs/*.ind docs/*.toc docs/*.ilg
	@${ENDRM} "RM" "Cleaned documentation"

clean-bin:
	@${BEGRM} "RM" "Cleaning native binaries..."
	@-rm -f hdd/bin/*
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
