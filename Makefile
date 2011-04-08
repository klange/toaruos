#CC = gcc-4.3
CC = clang
# Sometimes we just have to use GCC
GCC = gcc
# CFLAGS for core components
CFLAGS = -Wall -Wextra -pedantic -m32 -O0 -std=c99 -finline-functions -fno-stack-protector -nostdinc -ffreestanding -Wno-unused-function -Wno-unused-parameter -g
# CFLAGS for native utils
NATIVEFLAGS = -std=c99 -g -pedantic -Wall -Wextra -Wno-unused-parameter
# Linker for core
LD = ld -m elf_i386
YASM = yasm
ECHO = `which echo` -e
# Feel free to be specific, but I'd rather you not be.
MODULES = $(patsubst %.c,%.o,$(wildcard kernel/core/*.c))
FILESYSTEMS = $(patsubst %.c,%.o,$(wildcard kernel/core/fs/*.c))
VIDEODRIVERS = $(patsubst %.c,%.o,$(wildcard kernel/core/video/*.c))
BINARIES = initrd/bin/test initrd/bin/echo
UTILITIES = util/bin/readelf
EMU = qemu
GENEXT = genext2fs
DD = dd conv=notrunc

.PHONY: all system clean install run docs utils

all: system bootdisk.img docs utils
system: toaruos-initrd toaruos-kernel

install: toaruos-initrd toaruos-kernel
	@${ECHO} -n "\033[34m   --   Installing to /boot...\033[0m"
	@cp toaruos-kernel /boot/toaruos-kernel
	@cp toaruos-initrd /boot/toaruos-initrd
	@${ECHO} "\r\033[34;1m   --   Kernel and ramdisk installed.\033[0m"

run: toaruos-kernel toaruos-initrd
	${EMU} -kernel toaruos-kernel -initrd toaruos-initrd -append vid=qemu -serial stdio -vga std

utils: ${UTILITIES}

#################
# Documentation #
#################
docs: docs/core.pdf

docs/core.pdf: docs/*.tex 
	@${ECHO} -n "\033[32m  docs  Generating documentation...\033[0m"
	@pdflatex -draftmode -halt-on-error -output-directory docs/ docs/core.tex > /dev/null
	@makeindex -q docs/*.idx
	@pdflatex -halt-on-error -output-directory docs/ docs/core.tex > /dev/null
	@${ECHO} "\r\033[32;1m  docs  Generated documentation PDF.\033[0m"

################
#    Kernel    #
################
toaruos-kernel: kernel/start.o kernel/link.ld kernel/main.o ${MODULES} ${FILESYSTEMS} ${VIDEODRIVERS}
	@${ECHO} -n "\033[32m   LD   $<\033[0m"
	@${LD} -T kernel/link.ld -o toaruos-kernel kernel/*.o kernel/core/*.o kernel/core/fs/*.o kernel/core/video/*.o
	@${ECHO} "\r\033[32;1m   LD   $<\033[0m"
	@${ECHO} "\033[34;1m   --   Kernel is ready!\033[0m"

kernel/start.o: kernel/start.s
	@${ECHO} -n "\033[32m  yasm  $<\033[0m"
	@${YASM} -f elf -o kernel/start.o kernel/start.s
	@${ECHO} "\r\033[32;1m  yasm  $<\033[0m"

%.o: %.c
	@${ECHO} -n "\033[32m   CC   $<\033[0m"
	@${CC} ${CFLAGS} -I./kernel/include -c -o $@ $<
	@${ECHO} "\r\033[32;1m   CC   $<\033[0m"

################
#   Ram disk   #
################
toaruos-initrd: initrd/boot/kernel initrd/boot/stage2 initrd/bs.bmp ${BINARIES}
	@${ECHO} -n "\033[32m initrd Generating initial RAM disk\033[0m"
	@# Get rid of the old one
	@-rm -f toaruos-initrd
	@${GENEXT} -d initrd -q -b 1024 toaruos-initrd
	@${ECHO} "\r\033[32;1m initrd Generated initial RAM disk image\033[0m"
	@${ECHO} "\033[34;1m   --   HDD image is ready!\033[0m"

### Ram Disk installers...

# Second stage bootloader
initrd/boot/stage2: bootloader/stage2.bin
	@mkdir -p initrd/boot
	@cp bootloader/stage2.bin initrd/boot/stage2

# Kernel
initrd/boot/kernel: toaruos-kernel
	@mkdir -p initrd/boot
	@cp toaruos-kernel initrd/boot/kernel

################
#   Utilities  #
################

util/bin/mrboots-installer: util/mrboots-installer.c
	@${ECHO} -n "\033[32m   CC   $<\033[0m"
	@${CC} ${NATIVEFLAGS} -o $@ $<
	@${ECHO} "\r\033[32;1m   CC   $<\033[0m"

util/bin/readelf: util/readelf.c
	@${ECHO} -n "\033[32m   CC   $<\033[0m"
	@${CC} ${NATIVEFLAGS} -o $@ $<
	@${ECHO} "\r\033[32;1m   CC   $<\033[0m"

################
#   Userspace  #
################
loader/crtbegin.o: loader/crtbegin.s
	@${ECHO} -n "\033[32m  yasm  $<\033[0m"
	@${YASM} -f elf32 -o $@ $<
	@${ECHO} "\r\033[32;1m  yasm  $<\033[0m"

initrd/bin/%: loader/%.o loader/crtbegin.o
	@${ECHO} -n "\033[32m   LD   $<\033[0m"
	@${LD} -T loader/link.ld -o $@ $<
	@${ECHO} "\r\033[32;1m   LD   $<\033[0m"

loader/%.o: loader/%.c
	@${ECHO} -n "\033[32m   CC   $<\033[0m"
	@${CC} ${CFLAGS} -c -o $@ $<
	@${ECHO} "\r\033[32;1m   CC   $<\033[0m"

################
#  Bootloader  #
################

# Stage 1
bootloader/stage1/main.o: bootloader/stage1/main.c
	@${ECHO} -n "\033[32m   CC   $<\033[0m"
	@${GCC} ${CFLAGS} -c -o $@ $<
	@${ECHO} "\r\033[32;1m   CC   $<\033[0m"

bootloader/stage1/start.o: bootloader/stage1/start.s
	@${ECHO} -n "\033[32m  yasm  $<\033[0m"
	@${YASM} -f elf32 -p gas -o $@ $<
	@${ECHO} "\r\033[32;1m  yasm  $<\033[0m"

bootloader/stage1.bin: bootloader/stage1/main.o bootloader/stage1/start.o bootloader/stage1/link.ld
	@${ECHO} -n "\033[32m   ld   $<\033[0m"
	@${LD} -o bootloader/stage1.bin -T bootloader/stage1/link.ld bootloader/stage1/start.o bootloader/stage1/main.o
	@${ECHO} "\r\033[32;1m   ld   $<\033[0m"

# Stage 2
bootloader/stage2/main.o: bootloader/stage2/main.c
	@${ECHO} -n "\033[32m   CC   $<\033[0m"
	@${GCC} ${CFLAGS} -I./kernel/include -c -o $@ $<
	@${ECHO} "\r\033[32;1m   CC   $<\033[0m"

bootloader/stage2/start.o: bootloader/stage2/start.s
	@${ECHO} -n "\033[32m  yasm  $<\033[0m"
	@${YASM} -f elf32 -p gas -o $@ $<
	@${ECHO} "\r\033[32;1m  yasm  $<\033[0m"

bootloader/stage2.bin: bootloader/stage2/main.o bootloader/stage2/start.o bootloader/stage2/link.ld
	@${ECHO} -n "\033[32m   ld   $<\033[0m"
	@${LD} -o bootloader/stage2.bin -T bootloader/stage2/link.ld bootloader/stage2/start.o bootloader/stage2/main.o
	@${ECHO} "\r\033[32;1m   ld   $<\033[0m"

##############
#  bootdisk  #
##############
bootdisk.img: bootloader/stage1.bin bootloader/stage2.bin util/bin/mrboots-installer
	@${ECHO} -n "\033[34m   --   Building bootdisk.img...\033[0m"
	@cat bootloader/stage1.bin bootloader/stage2.bin > bootdisk.img
	@${ECHO} "\r\033[34;1m   --   Bootdisk is ready!      \033[0m"

###############
#    clean    #
###############
clean:
	@${ECHO} -n "\033[31m   RM   Cleaning... \033[0m"
	@-rm -f toaruos-kernel
	@-rm -f toaruos-initrd
	@-rm -f kernel/*.o
	@-rm -f kernel/core/*.o
	@-rm -f kernel/core/fs/*.o
	@-rm -f kernel/core/video/*.o
	@-rm -f bootloader/stage1.bin
	@-rm -f bootloader/stage1/*.o
	@-rm -f bootloader/stage2.bin
	@-rm -f bootloader/stage2/*.o
	@-rm -f initrd/bin/*
	@-rm -f loader/*.o
	@-rm -f -r initrd/boot
	@-rm -f bootdisk.img
	@-rm -f docs/*.pdf docs/*.aux docs/*.log docs/*.out
	@-rm -f docs/*.idx docs/*.ind docs/*.toc docs/*.ilg
	@-rm -f util/bin/*
	@${ECHO} "\r\033[31;1m   RM   Finished cleaning.\033[0m\033[K"
