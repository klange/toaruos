ARCH=aarch64

ARCH_KERNEL_CFLAGS = -z max-page-size=0x1000 -nostdlib -mgeneral-regs-only -mno-outline-atomics -ffixed-x18 --sysroot=base
ARCH_USER_CFLAGS = -Wno-psabi --sysroot=base

TARGET=aarch64-unknown-toaru

all: system
system: misaka-kernel ramdisk.igz bootstub kernel8.img | $(BUILD_KRK)

misaka-kernel: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o kernel/arch/aarch64/link.ld
	${CC} -g -T kernel/arch/${ARCH}/link.ld ${KERNEL_CFLAGS} -o $@ ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o

BOOTSTUB_OBJS  = $(patsubst %.c,%.o,$(wildcard kernel/arch/aarch64/bootstub/*.c))
BOOTSTUB_OBJS += $(patsubst %.S,%.o,$(wildcard kernel/arch/aarch64/bootstub/*.S))
BOOTSTUB_OBJS += kernel/misc/kprintf.o kernel/misc/string.o

bootstub: ${BOOTSTUB_OBJS} kernel/arch/aarch64/bootstub/link.ld
	${CC} -g -T kernel/arch/aarch64/bootstub/link.ld ${KERNEL_CFLAGS} -o $@ ${BOOTSTUB_OBJS}


RPI400_OBJS  = $(patsubst %.c,%.o,$(wildcard kernel/arch/aarch64/rpi400/*.c))
RPI400_OBJS += $(patsubst %.S,%.o,$(wildcard kernel/arch/aarch64/rpi400/*.S))
RPI400_OBJS += kernel/misc/kprintf.o kernel/misc/string.o

kernel/arch/aarch64/rpi400/start.o: misaka-kernel ramdisk.igz
kernel8.img: ${RPI400_OBJS} kernel/arch/aarch64/rpi400/link.ld
	${CC} -g -T kernel/arch/aarch64/rpi400/link.ld ${KERNEL_CFLAGS} -o $@.elf ${RPI400_OBJS}
	${OC} $@.elf -O binary $@

QEMU = qemu-system-aarch64

EMU_MACH = virt-2.12
EMU_CPU  = cortex-a72
SMP ?= 4
RAM ?= 4G

EMU_ARGS  = -M $(EMU_MACH)
EMU_ARGS += -m $(RAM)
EMU_ARGS += -smp $(SMP)
EMU_ARGS += -cpu $(EMU_CPU)
EMU_ARGS += -no-reboot
EMU_ARGS += -serial mon:stdio
EMU_ARGS += -device bochs-display
EMU_ARGS += -device virtio-tablet-pci    # Mouse with absolute positioning
EMU_ARGS += -device virtio-keyboard-pci  # Keyboard
EMU_ARGS += -device AC97
EMU_ARGS += -d guest_errors
EMU_ARGS += -net user
EMU_ARGS += -netdev hubport,id=u1,hubid=0, -device e1000e,netdev=u1
EMU_ARGS += -name "ToaruOS ${ARCH}"

EMU_RAMDISK = -fw_cfg name=opt/org.toaruos.initrd,file=ramdisk.igz
EMU_KERNEL  = -fw_cfg name=opt/org.toaruos.kernel,file=misaka-kernel

run: system
	${QEMU} ${EMU_ARGS} -kernel bootstub  -append "root=/dev/ram0 migrate start=live-session vid=auto" ${EMU_RAMDISK} ${EMU_KERNEL}

hvf: EMU_MACH = virt-2.12
hvf: EMU_CPU = host -accel hvf
hvf: system
	${QEMU} ${EMU_ARGS} -kernel bootstub  -append "root=/dev/ram0 migrate start=live-session vid=auto" ${EMU_RAMDISK} ${EMU_KERNEL}

debug: system
	${QEMU} ${EMU_ARGS} -kernel bootstub  -append "root=/dev/ram0 migrate start=live-session vid=auto" ${EMU_RAMDISK} ${EMU_KERNEL} -d int 2>&1

BUILD_KRK=$(TOOLCHAIN)/local/bin/kuroko
$(TOOLCHAIN)/local/bin/kuroko: kuroko/src/*.c
	mkdir -p $(TOOLCHAIN)/local/bin
	cc -Ikuroko/src -DKRK_BUNDLE_LIBS="BUNDLED(os);BUNDLED(fileio);" -DNO_RLINE -DKRK_STATIC_ONLY -DKRK_DISABLE_THREADS -o "${TOOLCHAIN}/local/bin/kuroko" kuroko/src/*.c kuroko/src/modules/module_os.c kuroko/src/modules/module_fileio.c

