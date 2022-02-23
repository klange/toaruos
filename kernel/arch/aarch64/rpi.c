/**
 * @file  kernel/arch/aarch64/rpi.c
 * @brief Raspberry Pi-specific stuff.
 *
 * Probably going to be mailbox interfaces and such.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2022 K. Lange
 */
#include <stdint.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/gzip.h>
#include <kernel/mmu.h>

#include <kernel/arch/aarch64/rpi.h>

extern char end[];

void rpi_load_ramdisk(struct rpitag * tag, uintptr_t * ramdisk_phys_base, size_t * ramdisk_size) {
	dprintf("rpi: compressed ramdisk is at %#x \n", tag->ramdisk_start);
	dprintf("rpi: end of ramdisk is at %#x \n", tag->ramdisk_end);
	dprintf("rpi: uncompress ramdisk to %#zx \n", (uintptr_t)&end);
	uint32_t size;
	memcpy(&size, (void*)(uintptr_t)(tag->ramdisk_end - sizeof(uint32_t)), sizeof(uint32_t));
	dprintf("rpi: size of uncompressed ramdisk is %#x\n", size);

	gzip_inputPtr  = (uint8_t*)(uintptr_t)tag->ramdisk_start;
	gzip_outputPtr = (uint8_t*)&end;

	if (gzip_decompress()) {
		dprintf("rpi: gzip failure, not mounting ramdisk\n");
		while (1);
	}

	dprintf("rpi: ramdisk decompressed\n");

	for (size_t i = 0; i < size; i += 64) {
		asm volatile ("dc cvac, %0\n" :: "r"((uintptr_t)&end + i) : "memory");
	}

	*ramdisk_phys_base = mmu_map_to_physical(NULL, (uintptr_t)&end);
	*ramdisk_size = size;

	dprintf("rpi: ramdisk_phys_base set to %#zx\n", *ramdisk_phys_base);
}

void rpi_set_cmdline(char ** args_out) {
	*args_out = (char *)"vid=preset start=live-session migrate root=/dev/ram0";
}
