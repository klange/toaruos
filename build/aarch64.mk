ARCH=aarch64

ARCH_KERNEL_CFLAGS = -z max-page-size=0x1000 -nostdlib -mgeneral-regs-only -mno-outline-atomics -ffixed-x18

TARGET=aarch64-unknown-toaru

all: system
system: misaka-kernel ramdisk.igz bootstub

misaka-kernel: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o kernel/arch/aarch64/link.ld
	${CC} -g -T kernel/arch/${ARCH}/link.ld ${KERNEL_CFLAGS} -o $@ ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o

BOOTSTUB_OBJS  = $(patsubst %.c,%.o,$(wildcard kernel/arch/aarch64/bootstub/*.c))
BOOTSTUB_OBJS += $(patsubst %.S,%.o,$(wildcard kernel/arch/aarch64/bootstub/*.S))
BOOTSTUB_OBJS += kernel/misc/kprintf.o kernel/misc/string.o

bootstub: ${BOOTSTUB_OBJS} kernel/arch/aarch64/bootstub/link.ld
	${CC} -g -T kernel/arch/aarch64/bootstub/link.ld ${KERNEL_CFLAGS} -o $@ ${BOOTSTUB_OBJS} -lgcc

QEMU = ~/Projects/third-party/qemu-git/build/qemu-system-aarch64

EMU_MACH = virt-2.12
EMU_CPU  = cortex-a72
SMP ?= 1
RAM ?= 4G

EMU_ARGS  = -M $(EMU_MACH)
EMU_ARGS += -m $(RAM)
EMU_ARGS += -smp $(SMP)
EMU_ARGS += -cpu $(EMU_CPU)
EMU_RAGS += -no-reboot
EMU_ARGS += -serial mon:stdio
EMU_ARGS += -device bochs-display
EMU_ARGS += -device virtio-tablet-pci    # Mouse with absolute positioning
EMU_ARGS += -device virtio-keyboard-pci  # Keyboard
EMU_ARGS += -d guest_errors

EMU_RAMDISK = -fw_cfg name=opt/org.toaruos.initrd,file=ramdisk.igz
EMU_KERNEL  = -fw_cfg name=opt/org.toaruos.kernel,file=misaka-kernel

run: system
	${QEMU} ${EMU_ARGS} -kernel bootstub  -append "root=/dev/ram0 migrate start=live-session vid=auto" ${EMU_RAMDISK} ${EMU_KERNEL}

debug: system
	${QEMU} ${EMU_ARGS} -kernel bootstub  -append "root=/dev/ram0 migrate start=live-session vid=auto" ${EMU_RAMDISK} ${EMU_KERNEL} -d int 2>&1

