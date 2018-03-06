KERNEL_TARGET=i686-elf
KCC = $(KERNEL_TARGET)-gcc
KAS = $(KERNEL_TARGET)-as
KLD = $(KERNEL_TARGET)-ld

all: image.iso

image.iso: stuff/boot/boot.sys
	xorriso -as mkisofs -R -J -c boot/bootcat -b boot/boot.sys -no-emul-boot -boot-load-size 4 -o image.iso stuff

cstuff.o: cstuff.c
	${KCC} -c -Os -o cstuff.o cstuff.c

boot.o: boot.s
	yasm -f elf -o $@ $<

stuff/boot/boot.sys: boot.o cstuff.o link.ld
	${KLD} -T link.ld -o $@ boot.o cstuff.o

