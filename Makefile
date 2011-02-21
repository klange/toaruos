#CC = gcc
CC = clang
CFLAGS = -Wall -Wextra -pedantic -m32 -O0 -std=c99 -finline-functions -fno-stack-protector -nostdinc -ffreestanding -Wno-unused-function -Wno-unused-parameter
LD = ld -m elf_i386
NASM = nasm
YASM = yasm
ECHO = `which echo` -e
MODULES = $(patsubst %.c,%.o,$(wildcard kernel/core/*.c))
FILESYSTEMS = $(patsubst %.c,%.o,$(wildcard kernel/core/fs/*.c))
EMU = qemu
GENEXT = genext2fs
DD = dd conv=notrunc

.PHONY: all clean install run

all: toaruos-kernel toaruos-initrd

install: toaruos-kernel toaruos-initrd
	@${ECHO} -n "\033[34m   --   Installing to /boot...\033[0m"
	@cp toaruos-kernel /boot/toaruos-kernel
	@cp toaruos-initrd /boot/toaruos-initrd
	@${ECHO} "\r\033[34;1m   --   Kernel and ramdisk installed.\033[0m"

run: toaruos-kernel toaruos-initrd
	${EMU} -kernel toaruos-kernel -initrd toaruos-initrd -serial stdio

################
#    Kernel    #
################
toaruos-kernel: kernel/start.o kernel/link.ld kernel/main.o ${MODULES} ${FILESYSTEMS}
	@${ECHO} -n "\033[32m   LD   $<\033[0m"
	@${LD} -T kernel/link.ld -o toaruos-kernel kernel/*.o kernel/core/*.o kernel/core/fs/*.o
	@${ECHO} "\r\033[32;1m   LD   $<\033[0m"

kernel/start.o: kernel/start.asm
	@${ECHO} -n "\033[32m  nasm  $<\033[0m"
	@${NASM} -f elf -o kernel/start.o kernel/start.asm
	@${ECHO} "\r\033[32;1m  nasm  $<\033[0m"

%.o: %.c
	@${ECHO} -n "\033[32m   CC   $<\033[0m"
	@${CC} ${CFLAGS} -I./kernel/include -c -o $@ $<
	@${ECHO} "\r\033[32;1m   CC   $<\033[0m"

################
#   Ram disk   #
################
toaruos-initrd: initrd bootloader/stage1 initrd/boot/stage2 initrd/boot/kernel
	@${ECHO} -n "\033[32m initrd Generating initial RAM disk\033[0m"
	@-rm -f toaruos-initrd
	@${GENEXT} -d initrd -q -b 249 toaruos-initrd
	@${DD} if=bootloader/stage1 of=toaruos-initrd 2>/dev/null
	@${ECHO} "\r\033[32;1m initrd Generated initial RAM disk image\033[0m"

### Ram Disk installers...

# Kernel
initrd/boot/kernel: toaruos-kernel
	@mkdir -p initrd/boot
	@cp toaruos-kernel initrd/boot/kernel

# Second-stage bootloader
initrd/boot/stage2: bootloader/stage2
	@mkdir -p initrd/boot
	@cp bootloader/stage2 initrd/boot/stage2

################
#  Bootloader  #
################
bootloader/stage1: bootloader/stage1.s
	@${ECHO} -n "\033[32m  nasm  $<\033[0m"
	@${NASM} -f bin -o $@ $<
	@${ECHO} "\r\033[32;1m  nasm  $<\033[0m"

bootloader/stage2: bootloader/stage2.s
	@${ECHO} -n "\033[32m  yasm  $<\033[0m"
	@${YASM} -p gas -f bin -w: -o $@ $<
	@${ECHO} "\r\033[32;1m  yasm  $<\033[0m"

bootloader/stage2.s: bootloader/stage2.c
	@${ECHO} -n "\033[32m   CC   $<\033[0m"
	@${CC} ${CFLAGS} -S -o $@ $<
	@${ECHO} "\r\033[32;1m   CC   $<\033[0m"

clean:
	@${ECHO} -n "\033[31m   RM   Cleaning... \033[0m"
	@-rm -f toaruos-kernel
	@-rm -f toaruos-initrd
	@-rm -f kernel/*.o
	@-rm -f kernel/core/*.o
	@-rm -f kernel/core/fs/*.o
	@-rm -f bootloader/stage1
	@-rm -f bootloader/stage2
	@-rm -f bootloader/stage2.s
	@-rm -f initrd/boot/stage2
	@-rm -f initrd/boot/kernel
	@${ECHO} "\r\033[31;1m   RM   Finished cleaning.\033[0m\033[K"
