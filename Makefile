ifeq ($(TOOLCHAIN),)
  ifeq ($(shell util/check.sh),y)
    export PATH := $(shell util/activate.sh)
  else
    FOO := $(shell util/prompt.sh)
    ifeq ($(shell util/check.sh),y)
      export PATH := $(shell util/activate.sh)
    else
      $(error "No toolchain, and you did not ask to build it.")
    endif
  endif
endif

# Prevents Make from removing intermediary files on failure
.SECONDARY:

# Disable built-in rules
.SUFFIXES:

all: image.iso

TARGET_TRIPLET=i686-pc-toaru

# Userspace flags

CC=$(TARGET_TRIPLET)-gcc
AR=$(TARGET_TRIPLET)-ar
AS=$(TARGET_TRIPLET)-as
CFLAGS= -O3 -g -std=gnu99 -I. -Iapps -pipe -mmmx -msse -msse2 -fplan9-extensions -Wall -Wextra -Wno-unused-parameter

##
# C library objects from libc/ C sources (and setjmp, which is assembly)
LIBC_OBJS  = $(patsubst %.c,%.o,$(wildcard libc/*.c))
LIBC_OBJS += $(patsubst %.c,%.o,$(wildcard libc/*/*.c))
LIBC_OBJS += libc/setjmp.o
LC=base/lib/libc.so

##
#  APPS      = C sources from apps/
#  APPS_X    = binaries
#  APPS_Y    = generated makefiles for binaries (except init)
#  APPS_SH   = shell scripts to copy to base/bin/ and mark executable
#  APPS_SH_X = destinations for shell scripts
APPS=$(patsubst apps/%.c,%,$(wildcard apps/*.c))
APPS_X=$(foreach app,$(APPS),base/bin/$(app))
APPS_Y=$(foreach app,$(APPS),.make/$(app).mak)
APPS_SH=$(patsubst apps/%.sh,%.sh,$(wildcard apps/*.sh))
APPS_SH_X=$(foreach app,$(APPS_SH),base/bin/$(app))

##
# LIBS   = C sources from lib/
# LIBS_X = Shared libraries (.so)
# LIBS_Y = Generated makefiles for libraries
LIBS=$(patsubst lib/%.c,%,$(wildcard lib/*.c))
LIBS_X=$(foreach lib,$(LIBS),base/lib/libtoaru_$(lib).so)
LIBS_Y=$(foreach lib,$(LIBS),.make/$(lib).lmak)

SOURCE_FILES  = $(wildcard kernel/*.c kernel/*/*.c kernel/*/*/*.c modules/*.c)
SOURCE_FILES += $(wildcard apps/*.c linker/*.c libc/*.c libc/*/*.c lib/*.c)

tags: $(SOURCE_FILES)
	ctags -f tags $(SOURCE_FILES)

##
# Files that must be present in the ramdisk (apps, libraries)
RAMDISK_FILES= ${APPS_X} ${APPS_SH_X} ${LIBS_X} base/lib/ld.so base/lib/libm.so

# Kernel / module flags

ifeq (,${USE_CLANG})
KCC = $(TARGET_TRIPLET)-gcc
LGCC = -lgcc
EXTRALIB = 
else
KCC = clang --target=i686-elf -static -Ibase/usr/include -nostdinc -mno-sse
LGCC = util/compiler-rt.o
EXTRALIB = util/compiler-rt.o
util/compiler-rt.o: util/compiler-rt.S
	${KAS} ${ASFLAGS} $< -o $@
endif
KAS = $(TARGET_TRIPLET)-as
KLD = $(TARGET_TRIPLET)-ld
KNM = $(TARGET_TRIPLET)-nm

KCFLAGS  = -O2 -std=c99
KCFLAGS += -finline-functions -ffreestanding
KCFLAGS += -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format
KCFLAGS += -pedantic -fno-omit-frame-pointer
KCFLAGS += -D_KERNEL_
KCFLAGS += -DKERNEL_GIT_TAG=$(shell util/make-version)
KASFLAGS = --32

##
# Kernel objects from kernel/ C sources
KERNEL_OBJS = $(patsubst %.c,%.o,$(wildcard kernel/*.c))
KERNEL_OBJS += $(patsubst %.c,%.o,$(wildcard kernel/*/*.c))
KERNEL_OBJS += $(patsubst %.c,%.o,$(wildcard kernel/*/*/*.c))

##
# Kernel objects from kernel/ assembly sources
KERNEL_ASMOBJS = $(filter-out kernel/symbols.o,$(patsubst %.S,%.o,$(wildcard kernel/*.S)))

# Kernel

fatbase/kernel: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o ${EXTRALIB}
	${KCC} -T kernel/link.ld ${KCFLAGS} -nostdlib -o $@ ${KERNEL_ASMOBJS} ${KERNEL_OBJS} kernel/symbols.o ${LGCC}

##
# Symbol table for the kernel. Instead of relying on getting
# the symbol table from our bootloader (eg. through ELF
# headers provided via multiboot structure), we have a dedicated
# object that build with all the symbols. This allows us to
# build the kernel as a flat binary or load it with less-capable
# multiboot loaders and still get symbols, which we need to
# load kernel modules and link them properly.
kernel/symbols.o: ${KERNEL_ASMOBJS} ${KERNEL_OBJS} util/generate_symbols.py ${EXTRALIB}
	-rm -f kernel/symbols.o
	${KCC} -T kernel/link.ld ${KCFLAGS} -nostdlib -o .toaruos-kernel ${KERNEL_ASMOBJS} ${KERNEL_OBJS} ${LGCC}
	${KNM} .toaruos-kernel -g | util/generate_symbols.py > kernel/symbols.S
	${KAS} ${KASFLAGS} kernel/symbols.S -o $@
	-rm -f .toaruos-kernel

##
# version.o should be rebuilt whenever the kernel changes
# in order to get fresh git commit hash information.
kernel/sys/version.o: kernel/*/*.c kernel/*.c

kernel/%.o: kernel/%.S
	${KAS} ${ASFLAGS} $< -o $@

kernel/%.o: kernel/%.c ${HEADERS}
	${KCC} ${KCFLAGS} -nostdlib -g -c -o $@ $<

# Modules

fatbase/mod:
	@mkdir -p $@

##
# Modules need to be installed on the boot image
MODULES = $(patsubst modules/%.c,fatbase/mod/%.ko,$(wildcard modules/*.c))
HEADERS = $(wildcard base/usr/include/kernel/*.h base/usr/include/kernel/*/*.h)

fatbase/mod/%.ko: modules/%.c ${HEADERS} | fatbase/mod
	${KCC} -nostdlib ${KCFLAGS} -c -o $@ $<

modules: ${MODULES}

# Root Filesystem

base/dev:
	mkdir -p $@
base/tmp:
	mkdir -p $@
base/proc:
	mkdir -p $@
base/bin:
	mkdir -p $@
base/lib:
	mkdir -p $@
base/cdrom:
	mkdir -p $@
base/var:
	mkdir -p $@
fatbase/efi/boot:
	mkdir -p $@
cdrom:
	mkdir -p $@
.make:
	mkdir -p .make
dirs: base/dev base/tmp base/proc base/bin base/lib base/cdrom cdrom base/var fatbase/efi/boot .make

# C Library

crts: base/lib/crt0.o base/lib/crti.o base/lib/crtn.o | dirs

base/lib/crt%.o: libc/crt%.S
	$(AS) -o $@ $<

libc/setjmp.o: libc/setjmp.S
	$(AS) -o $@ $<

libc/%.o: libc/%.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

base/lib/libc.a: ${LIBC_OBJS} | dirs crts
	$(AR) cr $@ $^

base/lib/libc.so: ${LIBC_OBJS} | dirs crts
	$(CC) -nodefaultlibs -o $@ $(CFLAGS) -shared -fPIC $^ -lgcc

base/lib/libm.so: util/lm.c | dirs crts
	$(CC) -nodefaultlibs -o $@ $(CFLAGS) -shared -fPIC $^ -lgcc

# Userspace Linker/Loader

base/lib/ld.so: linker/linker.c base/lib/libc.a | dirs
	$(CC) -static -Wl,-static $(CFLAGS) -o $@ -Os -T linker/link.ld $<

# Shared Libraries
.make/%.lmak: lib/%.c util/auto-dep.py | dirs crts
	util/auto-dep.py --makelib $< > $@

ifeq (,$(findstring clean,$(MAKECMDGOALS)))
-include ${LIBS_Y}
endif

# netinit needs to go in the CD/FAT root, so it gets built specially
fatbase/netinit: util/netinit.c base/lib/libc.a | dirs
	$(CC) $(CFLAGS) -o $@ $<

# Userspace applications

.make/%.mak: apps/%.c util/auto-dep.py | dirs crts
	util/auto-dep.py --make $< > $@

ifeq (,$(findstring clean,$(MAKECMDGOALS)))
-include ${APPS_Y}
endif

base/bin/%.sh: apps/%.sh
	cp $< $@
	chmod +x $@

# Ramdisk
fatbase/ramdisk.img: ${RAMDISK_FILES} $(shell find base) Makefile util/createramdisk.py | dirs
	python3 util/createramdisk.py

# CD image

ifeq (,$(wildcard /usr/lib32/crt0-efi-ia32.o))
$(error Missing GNU-EFI.)
endif

EFI_XORRISO=-eltorito-alt-boot -e fat.img -no-emul-boot -isohybrid-gpt-basdat
EFI_BOOT=cdrom/fat.img
EFI_UPDATE=util/update-extents.py

image.iso: ${EFI_BOOT} cdrom/boot.sys fatbase/netinit ${MODULES} util/update-extents.py
	xorriso -as mkisofs -R -J -c bootcat \
	  -b boot.sys -no-emul-boot -boot-load-size 24 \
	  ${EFI_XORRISO} \
	  -o image.iso cdrom
	${EFI_UPDATE}

# Boot loader

##
# FAT EFI payload
# This is the filesystem the EFI loaders see, so it must contain
# the kernel, modules, and ramdisk, plus anything else we want
# available to the bootloader (eg., netinit).
cdrom/fat.img: fatbase/ramdisk.img ${MODULES} fatbase/kernel fatbase/netinit fatbase/efi/boot/bootia32.efi fatbase/efi/boot/bootx64.efi util/mkdisk.sh | dirs
	util/mkdisk.sh $@ fatbase

##
# For EFI, we build two laoders: ia32 and x64
# We build them as ELF shared objects and the use objcopy to convert
# them to PE executables / DLLs (as expected by EFI).
EFI_CFLAGS=-fno-stack-protector -fpic -DEFI_PLATFORM -ffreestanding -fshort-wchar -I /usr/include/efi -mno-red-zone
EFI_SECTIONS=-j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc

# ia32
boot/efi.so: boot/cstuff.c boot/*.h
	$(CC) ${EFI_CFLAGS} -I /usr/include/efi/ia32 -c -o boot/efi.o $<
	$(LD) boot/efi.o /usr/lib32/crt0-efi-ia32.o -nostdlib -znocombreloc -T /usr/lib32/elf_ia32_efi.lds -shared -Bsymbolic -L /usr/lib32 -lefi -lgnuefi -o boot/efi.so

fatbase/efi/boot/bootia32.efi: boot/efi.so
	objcopy ${EFI_SECTIONS} --target=efi-app-ia32 $< $@

# x64
boot/efi64.so: boot/cstuff.c boot/*.h
	gcc ${EFI_CFLAGS} -I /usr/include/efi/x86_64 -DEFI_FUNCTION_WRAPPER -c -o boot/efi64.o $<
	$(LD) boot/efi64.o /usr/lib/crt0-efi-x86_64.o -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic -L /usr/lib -lefi -lgnuefi -o boot/efi64.so

fatbase/efi/boot/bootx64.efi: boot/efi64.so
	objcopy ${EFI_SECTIONS} --target=efi-app-x86_64 $< $@

# BIOS loader
cdrom/boot.sys: boot/boot.o boot/cstuff.o boot/link.ld | dirs
	${KLD} -T boot/link.ld -o $@ boot/boot.o boot/cstuff.o

boot/cstuff.o: boot/cstuff.c boot/*.h
	${CC} -c -Os -o $@ $<

boot/boot.o: boot/boot.S
	${AS} -o $@ $<

.PHONY: clean
clean:
	rm -f base/lib/*.so
	rm -f base/lib/libc.a
	rm -f ${APPS_X} ${APPS_SH_X}
	rm -f libc/*.o libc/*/*.o
	rm -f image.iso
	rm -f fatbase/ramdisk.img
	rm -f cdrom/boot.sys
	rm -f boot/*.o
	rm -f boot/*.efi
	rm -f boot/*.so
	rm -f cdrom/fat.img cdrom/kernel cdrom/mod/* cdrom/ramdisk.img
	rm -f fatbase/kernel fatbase/efi/boot/bootia32.efi fatbase/efi/boot/bootx64.efi
	rm -f cdrom/netinit fatbase/netinit
	rm -f ${KERNEL_OBJS} ${KERNEL_ASMOBJS} kernel/symbols.o kernel/symbols.S
	rm -f base/lib/crt*.o
	rm -f ${MODULES}
	rm -f ${APPS_Y} ${LIBS_Y} ${EXT_LIBS_Y}

ifneq (,$(findstring Microsoft,$(shell uname -r)))
  QEMU_ARGS=-serial mon:stdio -m 1G -rtc base=localtime -vnc :0
else
  ifeq (,${NO_KVM})
    KVM=-enable-kvm
  else
    KVM=
  endif
  QEMU_ARGS=-serial mon:stdio -m 1G -soundhw ac97,pcspk ${KVM} -rtc base=localtime ${QEMU_EXTRA}
endif


.PHONY: run
run: image.iso
	qemu-system-i386 -cdrom $< ${QEMU_ARGS}

.PHONY: fast
fast: image.iso
	qemu-system-i386 -cdrom $< ${QEMU_ARGS} \
	  -fw_cfg name=opt/org.toaruos.bootmode,string=normal

.PHONY: headless
headless: image.iso
	@qemu-system-i386 -cdrom $< -m 1G ${KVM} -rtc base=localtime ${QEMU_EXTRA} \
	  -serial null -serial mon:stdio \
	  -nographic -no-reboot -audiodev none,id=id \
	  -fw_cfg name=opt/org.toaruos.bootmode,string=headless \
	  -fw_cfg name=opt/org.toaruos.gettyargs,string="-a local /dev/ttyS1"

.PHONY: shell
shell: image.iso
	@qemu-system-i386 -cdrom $< ${QEMU_ARGS} \
	  -nographic -no-reboot \
	  -fw_cfg name=opt/org.toaruos.bootmode,string=headless \
	  -fw_cfg name=opt/org.toaruos.forceuser,string=local \
	  -fw_cfg name=opt/org.toaruos.term,string=${TERM} </dev/null >/dev/null & \
	  stty raw -echo && nc -l 127.0.0.1 8090 && stty sane && wait

.PHONY: efi64
efi64: image.iso
	qemu-system-x86_64 -cdrom $< ${QEMU_ARGS} \
	  -bios /usr/share/qemu/OVMF.fd


VMNAME=ToaruOS CD

define virtualbox-runner =
.PHONY: $1
$1: image.iso
	-VBoxManage unregistervm "$(VMNAME)" --delete
	VBoxManage createvm --name "$(VMNAME)" --ostype $2 --register
	VBoxManage modifyvm "$(VMNAME)" --memory 1024 --vram 32 --audio pulse --audiocontroller ac97 --bioslogodisplaytime 1 --bioslogofadeout off --bioslogofadein off --biosbootmenu disabled $3
	VBoxManage storagectl "$(VMNAME)" --add ide --name "IDE"
	VBoxManage storageattach "$(VMNAME)" --storagectl "IDE" --port 0 --device 0 --medium $$(shell pwd)/image.iso --type dvddrive
	VBoxManage setextradata "$(VMNAME)" GUI/DefaultCloseAction PowerOff
	VBoxManage startvm "$(VMNAME)" --type separate
endef

$(eval $(call virtualbox-runner,virtualbox,"Other",))
$(eval $(call virtualbox-runner,virtualbox-efi,"Other",--firmware efi))
$(eval $(call virtualbox-runner,virtualbox-efi64,"Other_64",--firmware efi))

##
# Optional Extensions
#
# These optional extension libraries require third-party components to build,
# but allow the native applications to make use of functionality such as
# TrueType fonts or PNG images. You must have the necessary elements to build
# these already installed into your sysroot for this to work.
EXT_LIBS=$(patsubst ext/%.c,%,$(wildcard ext/*.c))
EXT_LIBS_X=$(foreach lib,$(EXT_LIBS),base/lib/libtoaru_$(lib).so)
EXT_LIBS_Y=$(foreach lib,$(EXT_LIBS),.make/$(lib).elmak)

.make/%.elmak: ext/%.c util/auto-dep.py | dirs
	util/auto-dep.py --makelib $< > $@

ifeq (,$(findstring clean,$(MAKECMDGOALS)))
-include ${EXT_LIBS_Y}
endif

# Freetype: Terminal text rendering backend
ext-freetype: base/lib/libtoaru_ext_freetype_fonts.so

# Cairo: Compositor rendering backend
ext-cairo: base/lib/libtoaru_ext_cairo_renderer.so

# Other extra stuff
util/ungz: util/ungz.c
	$(CC) -o $@ $< -lz
