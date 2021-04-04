# Make rules for the BIOS and EFI loaders
TARGETS=../fatbase/efi/boot/bootx64.efi ../fatbase/efi/boot/bootia32.efi ../cdrom/boot.sys

all: ${TARGETS}

export PATH := $(shell ../util/activate.sh)
TARGET_TRIPLET=i686-pc-toaru
CC=$(TARGET_TRIPLET)-gcc
AR=$(TARGET_TRIPLET)-ar
AS=$(TARGET_TRIPLET)-as
KLD=$(TARGET_TRIPLET)-ld

KUROKO_CS=builtins.c chunk.c compiler.c debug.c exceptions.c memory.c obj_base.c obj_bytes.c obj_dict.c object.c obj_function.c obj_gen.c obj_list.c obj_numeric.c obj_range.c obj_set.c obj_str.c obj_tuple.c obj_typing.c scanner.c table.c value.c vm.c
KUROKO_SRC=$(patsubst %,../kuroko/src/%,${KUROKO_CS})
COMMON_SRC=$(sort $(wildcard *.c))
BIOS_SRC=$(sort $(wildcard bios/*.c))
EFI_SRC=$(sort $(wildcard efi/*.c))

EFI32_OBJS=$(patsubst %.c,%.efi32.o, ${COMMON_SRC} ${EFI_SRC} ${KUROKO_SRC})
EFI64_OBJS=$(patsubst %.c,%.efi64.o, ${COMMON_SRC} ${EFI_SRC} ${KUROKO_SRC})
BIOS_OBJS=$(patsubst %.c,%.bios.o, ${COMMON_SRC} ${BIOS_SRC} ${KUROKO_SRC}) boot.o

KUROKO_CFLAGS=-Ddouble=int -DSTATIC_ONLY -DKRK_NO_DOCUMENTATION -DSTRICTLY_NO_THREADS -DNO_SOURCE_IN_TRACEBACK -DNDEBUG -DNO_FILESYSTEM
COMMON_CFLAGS=${KUROKO_CFLAGS} -ffreestanding -I. -nostdinc -Iinclude
BIOS_CFLAGS=${COMMON_CFLAGS} -Os
EFI_CFLAGS=${COMMON_CFLAGS} -fno-stack-protector -fpic -DEFI_PLATFORM -fshort-wchar -I /usr/include/efi -mno-red-zone
EFI_SECTIONS=-j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc

%.efi32.o: %.c
	gcc -m32 ${EFI_CFLAGS} -I /usr/include/efi/ia32 -c -o $@ $<

efi32.so: ${EFI32_OBJS}
	ld ${EFI32_OBJS} /usr/lib32/crt0-efi-ia32.o -nostdlib -znocombreloc -T /usr/lib32/elf_ia32_efi.lds -shared -Bsymbolic -L /usr/lib32 -lefi -lgnuefi -o $@

../fatbase/efi/boot/bootia32.efi: efi32.so
	objcopy ${EFI_SECTIONS} --target=efi-app-ia32 $< $@

%.efi64.o: %.c
	gcc ${EFI_CFLAGS} -I /usr/include/efi/x86_64 -DEFI_FUNCTION_WRAPPER -c -o $@ $<

efi64.so: ${EFI64_OBJS}
	ld ${EFI64_OBJS} /usr/lib/crt0-efi-x86_64.o -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic -L /usr/lib -lefi -lgnuefi -o $@

../fatbase/efi/boot/bootx64.efi: efi64.so
	objcopy ${EFI_SECTIONS} --target=efi-app-x86_64 $< $@

%.bios.o: %.c
	$(CC) ${BIOS_CFLAGS} -c -o $@ $<

boot.o: boot.S
	${AS} -o $@ $<

../cdrom/boot.sys: ${BIOS_OBJS} link.ld
	${KLD} -T link.ld -o $@ ${BIOS_OBJS}

.PHONY: clean
clean:
	@rm -f ${EFI32_OBJS} ${EFI64_OBJS} ${BIOS_OBJS} efi32.so efi64.so ${TARGETS}
