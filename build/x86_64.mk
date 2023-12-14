ARCH=x86_64

ARCH_KERNEL_CFLAGS  = -mno-red-zone -fno-omit-frame-pointer -mfsgsbase -fPIE
ARCH_KERNEL_CFLAGS += -mgeneral-regs-only -z max-page-size=0x1000 -nostdlib
ARCH_USER_CFLAGS += -z max-page-size=0x1000

TARGET=x86_64-pc-toaru

# Configs you can override.
#   SMP: Argument to -smp, use 1 to disable SMP.
#   RAM: Argument to -m, QEMU takes suffixes like "M" or "G".
#   EXTRA_ARGS: Added raw to the QEMU command line
#   EMU_KVM: Unset this (EMU_KVM=) to use TCG, or replace it with something like EMU_KVM=-enable-haxm
#   EMU_MACH: Argument to -M, 'pc' should be the older default in QEMU; we use q35 to test AHCI.
SMP ?= 4
RAM ?= 3G
EXTRA_ARGS ?=
EMU_MACH ?= q35

EMU_KVM  ?= -enable-kvm

EMU_ARGS  = -M q35
EMU_ARGS += -m $(RAM)
EMU_ARGS += -smp $(SMP)
EMU_ARGS += ${EMU_KVM}
EMU_ARGS += -no-reboot
EMU_ARGS += -serial mon:stdio
EMU_ARGS += -device AC97
EMU_ARGS += -name "ToaruOS ${ARCH}"

# UTC is the default setting.
#EMU_ARGS += -rtc base=utc

# Customize network options here. QEMU's default is an e1000(e) under PIIX (Q35), with user networking
# so we don't need to do anything normally.
#EMU_ARGS += -net user
#EMU_ARGS += -netdev hubport,id=u1,hubid=0, -device e1000e,netdev=u1  -object filter-dump,id=f1,netdev=u1,file=qemu-e1000e.pcap
#EMU_ARGS += -netdev hubport,id=u2,hubid=0, -device e1000e,netdev=u2

# Add an XHCI tablet if you want to dev on USB
#EMU_ARGS += -device qemu-xhci -device usb-tablet

all: system
system: image.iso

run: system
	${EMU} ${EMU_ARGS} -cdrom image.iso

fast: system
	${EMU} ${EMU_ARGS} -cdrom image.iso \
		-fw_cfg name=opt/org.toaruos.bootmode,string=normal \

run-vga: system
	${EMU} ${EMU_ARGS} -cdrom image.iso \
		-fw_cfg name=opt/org.toaruos.bootmode,string=vga \

test: system
	${EMU} -M ${EMU_MACH} -m $(RAM) -smp $(SMP) ${EMU_KVM} -kernel misaka-kernel -initrd ramdisk.igz,util/init.krk -append "root=/dev/ram0 init=/dev/ram1" \
		-nographic -no-reboot -audiodev none,id=id -serial null -serial mon:stdio \
		-device qemu-xhci -device usb-tablet -trace "usb*"

shell: system
	${EMU} -M ${EMU_MACH} -m $(RAM) -smp $(SMP) ${EMU_KVM} -cdrom image.iso \
		-nographic -no-reboot -audiodev none,id=id -serial null -serial mon:stdio \
		-fw_cfg name=opt/org.toaruos.gettyargs,string="-a local /dev/ttyS1 115200 ${TERM}" \
		-fw_cfg name=opt/org.toaruos.bootmode,string=headless

misaka-kernel: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o
	${CC} -g -T kernel/arch/${ARCH}/link.ld ${KERNEL_CFLAGS} -Wl,-static,-pie,--no-dynamic-linker,-z,notext,-z,norelro -o $@.64 ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o
	cp $@.64 $@
	${STRIP} $@

# Loader stuff, legacy CDs
fatbase/ramdisk.igz: ramdisk.igz
	cp $< $@
fatbase/kernel: misaka-kernel
	cp $< $@

cdrom/fat.img: fatbase/ramdisk.igz fatbase/kernel fatbase/efi/boot/bootx64.efi util/mkdisk.sh | dirs
	util/mkdisk.sh $@ fatbase

cdrom/boot.sys: boot/bios/boot.o $(patsubst boot/%.c,boot/bios/%.o,$(wildcard boot/*.c)) boot/link.ld | dirs
	${LD} -melf_i386 -T boot/link.ld -o $@ boot/bios/boot.o $(patsubst boot/%.c,boot/bios/%.o,$(wildcard boot/*.c))

boot/bios/%.o: boot/%.c boot/*.h | dirs
	${CC} -m32 -c -Os -fno-pic -fno-pie -fno-strict-aliasing -finline-functions -ffreestanding -mgeneral-regs-only -o $@ $<

boot/bios/boot.o: boot/boot.S | dirs
	${AS} --32 -o $@ $<

EFI_CFLAGS=-fno-stack-protector -fpic -DEFI_PLATFORM -ffreestanding -fshort-wchar -I /usr/include/efi -mno-red-zone
EFI_SECTIONS=-j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc
EFI_LINK=/usr/lib/crt0-efi-x86_64.o -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic -L /usr/lib -lefi -lgnuefi

boot/efi/%.o: boot/%.c boot/*.h | dirs
	$(CC) ${EFI_CFLAGS} -I /usr/include/efi/x86_64 -DEFI_FUNCTION_WRAPPER -c -o $@ $<

boot/efi64.so: $(patsubst boot/%.c,boot/efi/%.o,$(wildcard boot/*.c)) boot/*.h
	$(LD) $(patsubst boot/%.c,boot/efi/%.o,$(wildcard boot/*.c)) ${EFI_LINK} -o $@

fatbase/efi/boot/bootx64.efi: boot/efi64.so
	mkdir -p fatbase/efi/boot
	objcopy ${EFI_SECTIONS} --target=efi-app-x86_64 $< $@

BUILD_KRK=$(TOOLCHAIN)/local/bin/kuroko
$(TOOLCHAIN)/local/bin/kuroko: kuroko/src/*.c
	mkdir -p $(TOOLCHAIN)/local/bin
	cc -Ikuroko/src -DKRK_BUNDLE_LIBS="BUNDLED(os);BUNDLED(fileio);" -DNO_RLINE -DKRK_STATIC_ONLY -DKRK_DISABLE_THREADS -o "${TOOLCHAIN}/local/bin/kuroko" kuroko/src/*.c kuroko/src/modules/module_os.c kuroko/src/modules/module_fileio.c

image.iso: cdrom/fat.img cdrom/boot.sys boot/mbr.S util/update-extents.krk | $(BUILD_KRK)
	xorriso -as mkisofs -R -J -c bootcat \
	  -b boot.sys -no-emul-boot -boot-load-size full \
	  -eltorito-alt-boot -e fat.img -no-emul-boot -isohybrid-gpt-basdat \
	  -o image.iso cdrom
	${AS} --32 $$(kuroko util/make_mbr.krk) -o boot/mbr.o boot/mbr.S
	${LD} -melf_i386 -T boot/link.ld -o boot/mbr.sys boot/mbr.o
	tail -c +513 image.iso > image.dat
	cat boot/mbr.sys image.dat > image.iso
	rm image.dat
	kuroko util/update-extents.krk

