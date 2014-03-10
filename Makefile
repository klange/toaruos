# ToAruOS Primary Build Script

# We always build with our targetted cross-compiler
ifneq ($(CCC_ANALYZE),yes)
CC = i686-pc-toaru-gcc
endif

# Build flags
CFLAGS  = -g -O0 -std=c99
CFLAGS += -finline-functions -ffreestanding
CFLAGS += -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
CFLAGS += -Wstrict-prototypes -pedantic

# We have some pieces of assembly sitting around as well...
YASM = yasm

# All of the core parts of the kernel are built directly.
# TODO: Modules would be fantastic
KERNEL_OBJS  = $(patsubst %.c,%.o,$(wildcard kernel/*/*.c))
KERNEL_OBJS += $(patsubst %.c,%.o,$(wildcard kernel/*/*/*.c))

# Loadable modules
MODULES = $(patsubst modules/%.c,hdd/mod/%.ko,$(wildcard modules/*.c))

# We also want to rebuild when a header changes.
# This is a naive approach, but it works...
HEADERS     = $(shell find kernel/include/ -type f -name '*.h')

# We'll call out to our userspace build script if we
# see changes to any of the userspace sources as well.
USERSPACE  = $(shell find userspace/ -type f -name '*.c')
USERSPACE += $(shell find userspace/ -type f -name '*.cpp')
USERSPACE += $(shell find userspace/ -type f -name '*.h')

# Pretty output utilities.
BEG = util/mk-beg
END = util/mk-end
INFO = util/mk-info
ERRORS = 2>>/tmp/.`whoami`-build-errors || util/mk-error
ERRORSS = >>/tmp/.`whoami`-build-errors || util/mk-error
BEGRM = util/mk-beg-rm
ENDRM = util/mk-end-rm

# Hard disk image generation
GENEXT = genext2fs
DISK_SIZE = `util/disk_size.sh`
DD = dd conv=notrunc

# Emulator settings
EMU = qemu-system-i386
EMUARGS  = -sdl -kernel toaruos-kernel -m 1024
EMUARGS += -serial stdio -vga std
EMUARGS += -hda toaruos-disk.img -k en-us -no-frame
EMUARGS += -rtc base=localtime -net nic,model=rtl8139 -net user
EMUKVM   = -enable-kvm

.PHONY: all system clean clean-once clean-hard clean-soft clean-bin clean-aux clean-core install run test-thing

# Prevents Make from removing intermediary files on failure
.SECONDARY: 

# Disable built-in rules
.SUFFIXES: 

all: .passed system tags
system: .passed toaruos-disk.img toaruos-kernel ${MODULES}

install: system
	@${BEG} "CP" "Installing to /boot..."
	@cp toaruos-kernel /boot/toaruos-kernel
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
	python2 util/run-tests.py 2>/dev/null

.passed:
	@util/check-reqs > /dev/null
	@touch .passed

################
#    Kernel    #
################
toaruos-kernel: kernel/start.o kernel/link.ld kernel/main.o kernel/symbols.o ${KERNEL_OBJS} .passed
	@${BEG} "CC" "$<"
	@${CC} -T kernel/link.ld -nostdlib -o toaruos-kernel kernel/*.o ${KERNEL_OBJS} -lgcc ${ERRORS}
	@${END} "CC" "$<"
	@${INFO} "--" "Kernel is ready!"

kernel/symbols.o: ${KERNEL_OBJS} util/generate_symbols.py
	@-rm -f kernel/symbols.o
	@${CC} -T kernel/link.ld -nostdlib -o toaruos-kernel kernel/*.o ${KERNEL_OBJS} -lgcc ${ERRORS}
	@i686-pc-toaru-nm toaruos-kernel -g | python2 util/generate_symbols.py > kernel/symbols.s
	@${BEG} "yasm" "kernel/symbols.s"
	@${YASM} -f elf -o $@ kernel/symbols.s ${ERRORS}
	@${END} "yasm" "kernel/symbols.s"

kernel/start.o: kernel/start.s
	@${BEG} "yasm" "$<"
	@${YASM} -f elf -o $@ $< ${ERRORS}
	@${END} "yasm" "$<"

kernel/sys/version.o: kernel/*/*.c kernel/*.c

hdd/mod/%.ko: modules/%.c
	@${BEG} "CC" "$< [module]"
	@${CC} -T modules/link.ld -I./kernel/include -nostdlib ${CFLAGS} -c -o $@ $< ${ERRORS}
	@${END} "CC" "$< [module]"

%.o: %.c ${HEADERS}
	@${BEG} "CC" "$<"
	@${CC} ${CFLAGS} -I./kernel/include -c -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

####################
# Hard Disk Images #
####################

.userspace-check: ${USERSPACE}
	@cd userspace && python2 build.py
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
	@-rm -f ${KERNEL_OBJS}
	@${ENDRM} "RM" "Cleaned modules."

clean-bin:
	@${BEGRM} "RM" "Cleaning native binaries..."
	@-rm -f hdd/bin/*
	@${ENDRM} "RM" "Cleaned native binaries"

clean-core:
	@${BEGRM} "RM" "Cleaning final output..."
	@-rm -f toaruos-kernel
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
