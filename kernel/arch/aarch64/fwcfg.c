/**
 * @file  kernel/arch/aarch64/fwcfg.c
 * @brief Methods for dealing with QEMU's fw-cfg interface on aarch64.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2022 K. Lange
 */
#include <stdint.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/gzip.h>
#include <kernel/mmu.h>

#include <kernel/arch/aarch64/dtb.h>

static struct fwcfg_dma {
	volatile uint32_t control;
	volatile uint32_t length;
	volatile uint64_t address;
} dma __attribute__((aligned(4096)));

void fwcfg_load_initrd(uintptr_t * ramdisk_phys_base, size_t * ramdisk_size) {
	uintptr_t z = 0;
	size_t z_pages= 0;
	uintptr_t uz = 0;
	size_t uz_pages = 0;

	extern char end[];
	uintptr_t ramdisk_map_start = mmu_map_to_physical(NULL, (uintptr_t)&end);

	/* See if we can find a qemu fw_cfg interface, we can use that for a ramdisk */
	uint32_t * fw_cfg = dtb_find_node_prefix("fw-cfg");
	if (fw_cfg) {
		dprintf("fw-cfg: found interface\n");
		/* best guess until we bother parsing these */
		uint32_t * regs = dtb_node_find_property(fw_cfg, "reg");
		if (regs) {
			volatile uint8_t * fw_cfg_addr = (volatile uint8_t*)(uintptr_t)(mmu_map_from_physical(swizzle(regs[3])));
			volatile uint64_t * fw_cfg_data = (volatile uint64_t *)fw_cfg_addr;
			volatile uint32_t * fw_cfg_32   = (volatile uint32_t *)fw_cfg_addr;
			volatile uint16_t * fw_cfg_sel  = (volatile uint16_t *)(fw_cfg_addr + 8);

			*fw_cfg_sel = 0;

			uint64_t response = fw_cfg_data[0];
			(void)response;

			/* Needs to be big-endian */
			*fw_cfg_sel = swizzle16(0x19);

			/* count response is 32-bit BE */
			uint32_t count = swizzle(fw_cfg_32[0]);

			struct fw_cfg_file {
				uint32_t size;
				uint16_t select;
				uint16_t reserved;
				char name[56];
			};

			struct fw_cfg_file file;
			uint8_t * tmp = (uint8_t *)&file;

			/* Read count entries */
			for (unsigned int i = 0; i < count; ++i) {
				for (unsigned int j = 0; j < sizeof(struct fw_cfg_file); ++j) {
					tmp[j] = fw_cfg_addr[0];
				}

				/* endian swap to get file size and selector ID */
				file.size = swizzle(file.size);
				file.select = swizzle16(file.select);

				if (!strcmp(file.name,"opt/org.toaruos.initrd")) {
					dprintf("fw-cfg: initrd found\n");
					z_pages = (file.size + 0xFFF) / 0x1000;
					z = ramdisk_map_start;
					ramdisk_map_start += z_pages * 0x1000;
					uint8_t * x = mmu_map_from_physical(z);

					dma.control = swizzle((file.select << 16) | (1 << 3) | (1 << 1));
					dma.length  = swizzle(file.size);
					dma.address = swizzle64(z);

					asm volatile ("isb" ::: "memory");
					fw_cfg_data[2] = swizzle64(mmu_map_to_physical(NULL,(uint64_t)&dma));
					asm volatile ("isb" ::: "memory");

					if (dma.control) {
						dprintf("fw-cfg: Error on DMA read (control: %#x)\n", dma.control);
						return;
					}

					dprintf("fw-cfg: initrd loaded x=%#zx\n", (uintptr_t)x);

					if (x[0] == 0x1F && x[1] == 0x8B) {
						dprintf("fw-cfg: will attempt to read size from %#zx\n", (uintptr_t)(x + file.size - sizeof(uint32_t)));
						uint32_t size;
						memcpy(&size, (x + file.size - sizeof(uint32_t)), sizeof(uint32_t));
						dprintf("fw-cfg: compressed ramdisk unpacks to %u bytes\n", size);

						uz_pages = (size + 0xFFF) / 0x1000;
						uz = ramdisk_map_start;
						ramdisk_map_start += uz_pages * 0x1000;
						uint8_t * ramdisk = mmu_map_from_physical(uz);

						gzip_inputPtr = x;
						gzip_outputPtr = ramdisk;
						if (gzip_decompress()) {
							dprintf("fw-cfg: gzip failure, not mounting ramdisk\n");
							return;
						}

						memmove(x, ramdisk, size);

						dprintf("fw-cfg: Unpacked ramdisk at %#zx\n", (uintptr_t)ramdisk);
						*ramdisk_phys_base = z;
						*ramdisk_size = size;
					} else {
						dprintf("fw-cfg: Ramdisk at %#zx\n", (uintptr_t)x);
						*ramdisk_phys_base = z;
						*ramdisk_size = file.size;
					}

					return;
				}
			}
		}
	}
}

