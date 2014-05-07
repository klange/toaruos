# ToAruOS Primary Build Script
ifneq ($(MAKECMDGOALS),toolchain)
 ifeq ($(TOOLCHAIN),)
  $(error No toolchain available and you did not ask to build it. Did you forget to source the toolchain config?)
 endif
endif


# We always build with our targetted cross-compiler
CC = i686-pc-toaru-gcc
NM = i686-pc-toaru-nm
CXX= i686-pc-toaru-g++

# Build flags
CFLAGS  = -O2 -std=c99
CFLAGS += -finline-functions -ffreestanding
CFLAGS += -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
CFLAGS += -pedantic -fno-omit-frame-pointer
CFLAGS += -D_KERNEL_

# Kernel autoversioning with git sha
CFLAGS += -DKERNEL_GIT_TAG=`util/make-version`

# We have some pieces of assembly sitting around as well...
YASM = yasm

# All of the core parts of the kernel are built directly.
KERNEL_OBJS  = $(patsubst %.c,%.o,$(wildcard kernel/*/*.c))
KERNEL_OBJS += $(patsubst %.c,%.o,$(wildcard kernel/*/*/*.c))

# Loadable modules
MODULES = $(patsubst modules/%.c,hdd/mod/%.ko,$(wildcard modules/*.c))

# We also want to rebuild when a header changes.
# This is a naive approach, but it works...
HEADERS     = $(shell find kernel/include/ -type f -name '*.h')

# Userspace build flags
USER_CFLAGS   = -O3 -m32 -Wa,--32 -g -Iuserspace -std=c99 -U__STRICT_ANSI__
USER_CXXFLAGS = -O3 -m32 -Wa,--32 -g -Iuserspace
USER_BINFLAGS = 

# Userspace binaries and libraries
USER_CFILES   = $(shell find userspace -not -wholename '*/lib/*' -name '*.c')
USER_CXXFILES = $(shell find userspace -not -wholename '*/lib/*' -name '*.c++')
USER_LIBFILES = $(shell find userspace -wholename '*/lib/*' -name '*.c')

# Userspace output files (so we can define metatargets)
USERSPACE  = $(foreach file,$(USER_CFILES),$(patsubst %.c,hdd/bin/%,$(notdir ${file})))
USERSPACE += $(foreach file,$(USER_CXXFILES),$(patsubst %.c++,hdd/bin/%,$(notdir ${file})))
USERSPACE += $(foreach file,$(USER_LIBFILES),$(patsubst %.c,%.o,${file}))

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
BOOT_MODULES += packetfs

# This is kinda silly. We're going to form an -initrd argument..
# which is basically -initrd "hdd/mod/%.ko,hdd/mod/%.ko..."
# for each of the modules listed above in BOOT_MODULES
COMMA := ,
EMPTY := 
SPACE := $(EMPTY) $(EMPTY)
BOOT_MODULES_X = -initrd "$(subst $(SPACE),$(COMMA),$(foreach mod,$(BOOT_MODULES),hdd/mod/$(mod).ko))"

# Emulator settings
EMU = qemu-system-i386
EMUARGS  = -sdl -kernel toaruos-kernel -m 1024
EMUARGS += -serial stdio -vga std
EMUARGS += -hda toaruos-disk.img -k en-us -no-frame
EMUARGS += -rtc base=localtime -net nic,model=rtl8139 -net user -soundhw pcspk
EMUARGS += -net dump -no-kvm-irqchip 
EMUARGS += $(BOOT_MODULES_X)
EMUKVM   = -enable-kvm

DISK_ROOT = root=/dev/hda
VID_QEMU  = vid=qemu,,1280,,720
START_VGA = start=--vga
START_SINGLE = start=--single
WITH_LOGS = logtoserial=1

.PHONY: all system install test toolchain userspace modules
.PHONY: clean clean-soft clean-hard clean-user clean-mods clean-core clean-disk clean-once
.PHONY: run vga term headless
.PHONY: kvm vga-kvm term-kvm headless-kvm
.PHONY: debug debug-kvm debug-term debug-term-kvm

# Prevents Make from removing intermediary files on failure
.SECONDARY: 

# Disable built-in rules
.SUFFIXES: 

all: .passed system tags userspace
system: .passed toaruos-disk.img toaruos-kernel modules
userspace: ${USERSPACE}
modules: ${MODULES}

# Various different quick options
run: system
	${EMU} ${EMUARGS} -append "$(VID_QEMU) $(DISK_ROOT)"
kvm: system
	${EMU} ${EMUARGS} ${EMUKVM} -append "$(VID_QEMU) $(DISK_ROOT)"
debug: system
	${EMU} ${EMUARGS} -append "$(VID_QEMU) $(WITH_LOGS) $(DISK_ROOT)"
debug-kvm: system
	${EMU} ${EMUARGS} ${EMUKVM} -append "$(VID_QEMU) $(WITH_LOGS) $(DISK_ROOT)"
vga: system
	${EMU} ${EMUARGS} -append "$(START_VGA) $(DISK_ROOT)"
vga-kvm: system
	${EMU} ${EMUARGS} ${EMUKVM} -append "$(START_VGA) $(DISK_ROOT)"
term: system
	${EMU} ${EMUARGS} -append "$(VID_QEMU) $(START_SINGLE) $(DISK_ROOT)"
term-kvm: system
	${EMU} ${EMUARGS} ${EMUKVM} -append "$(VID_QEMU) $(START_SINGLE) $(DISK_ROOT)"
debug-term: system
	${EMU} ${EMUARGS} -append "$(VID_QEMU) $(START_SINGLE) $(WITH_LOGS) $(DISK_ROOT)"
debug-term-kvm: system
	${EMU} ${EMUARGS} ${EMUKVM} -append "$(VID_QEMU) $(START_SINGLE) $(WITH_LOGS) $(DISK_ROOT)"
headless: system
	${EMU} ${EMUARGS} -display none -append "$(START_VGA) $(DISK_ROOT)"
headless-kvm: system
	${EMU} ${EMUARGS} ${EMUKVM} -display none -append "$(START_VGA) $(DISK_ROOT)"

test: system
	python2 util/run-tests.py 2>/dev/null

toolchain:
	@cd toolchain; ./toolchain-build.sh

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
	@${BEG} "nm" "Generating symbol list..."
	@${CC} -T kernel/link.ld -nostdlib -o toaruos-kernel kernel/*.o ${KERNEL_OBJS} -lgcc ${ERRORS}
	@${NM} toaruos-kernel -g | python2 util/generate_symbols.py > kernel/symbols.s
	@${END} "nm" "Generated symbol list."
	@${BEG} "yasm" "kernel/symbols.s"
	@${YASM} -f elf -o $@ kernel/symbols.s ${ERRORS}
	@${END} "yasm" "kernel/symbols.s"

kernel/start.o: kernel/start.s
	@${BEG} "yasm" "$<"
	@${YASM} -f elf -o $@ $< ${ERRORS}
	@${END} "yasm" "$<"

kernel/sys/version.o: kernel/*/*.c kernel/*.c

hdd/mod/%.ko: modules/%.c ${HEADERS}
	@${BEG} "CC" "$< [module]"
	@${CC} -T modules/link.ld -I./kernel/include -nostdlib ${CFLAGS} -c -o $@ $< ${ERRORS}
	@${END} "CC" "$< [module]"

kernel/%.o: kernel/%.c ${HEADERS}
	@${BEG} "CC" "$<"
	@${CC} ${CFLAGS} -g -I./kernel/include -c -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

#############
# Userspace #
#############

# Libraries
userspace/%.o: userspace/%.c
	@${BEG} "CC" "$<"
	@${CC} ${USER_CFLAGS} $(shell util/auto-dep.py --cflags $<) -c -o $@ $< ${ERRORS}
	@${END} "CC" "$<"

# Binaries from C sources
define user-c-rule
$1: $2 $(shell util/auto-dep.py --deps $2)
	@${BEG} "CC" "$$<"
	@${CC} -o $$@ $(USER_CFLAGS) $(USER_BINFLAGS) $$(shell util/auto-dep.py --cflags $$<) $$< $$(shell util/auto-dep.py --libs $$<) ${ERRORS}
	@${END} "CC" "$$<"
endef
$(foreach file,$(USER_CFILES),$(eval $(call user-c-rule,$(patsubst %.c,hdd/bin/%,$(notdir ${file})),${file})))

# Binaries from C++ sources
define user-cxx-rule
$1: $2 $(shell util/auto-dep.py --deps $2)
	@${BEG} "C++" "$$<"
	@${CXX} -o $$@ $(USER_CXXFLAGS) $(USER_BINFLAGS) $$(shell util/auto-dep.py --cflags $$<) $$< $$(shell util/auto-dep.py --libs $$<) ${ERRORS}
	@${END} "C++" "$$<"
endef
$(foreach file,$(USER_CXXFILES),$(eval $(call user-cxx-rule,$(patsubst %.c++,hdd/bin/%,$(notdir ${file})),${file})))

####################
# Hard Disk Images #
####################

toaruos-disk.img: ${USERSPACE} ${MODULES}
	@${BEG} "hdd" "Generating a Hard Disk image..."
	@-rm -f toaruos-disk.img
	@${GENEXT} -B 4096 -d hdd -U -b ${DISK_SIZE} -N 4096 toaruos-disk.img ${ERRORS}
	@${END} "hdd" "Generated Hard Disk image"
	@${INFO} "--" "Hard disk image is ready!"

##############
#    ctags   #
##############
tags: kernel/*/*.c kernel/*.c userspace/**/*.c modules/*.c
	@${BEG} "ctag" "Generating CTags..."
	@-ctags -R --c++-kinds=+p --fields=+iaS --extra=+q kernel userspace modules util ${ERRORS}
	@${END} "ctag" "Generated CTags."

###############
#    clean    #
###############

clean-soft:
	@${BEGRM} "RM" "Cleaning modules..."
	@-rm -f kernel/*.o
	@-rm -f kernel/*/*.o
	@-rm -f ${KERNEL_OBJS}
	@${ENDRM} "RM" "Cleaned modules"

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

clean-once:
	@${BEGRM} "RM" "Cleaning one-time files..."
	@-rm -f .passed
	@${ENDRM} "RM" "Cleaned one-time files"

clean: clean-soft clean-core
	@${INFO} "--" "Finished soft cleaning"

clean-hard: clean clean-user clean-mods
	@${INFO} "--" "Finished hard cleaning"


# vim:noexpandtab
# vim:tabstop=4
# vim:shiftwidth=4
