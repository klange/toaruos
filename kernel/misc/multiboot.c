/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 *
 * Multiboot (GRUB) handler
 */
#include <system.h>
#include <logging.h>
#include <multiboot.h>

char * ramdisk = NULL;
struct multiboot * mboot_ptr = NULL;

struct multiboot *
copy_multiboot(
		struct multiboot *mboot_ptr
		) {
	struct multiboot *new_header = (struct multiboot *)kmalloc(sizeof(struct multiboot));
	memcpy(new_header, mboot_ptr, sizeof(struct multiboot));
	return new_header;
}

void
dump_multiboot(
		struct multiboot *mboot_ptr
		) {
	debug_print(INFO, "MULTIBOOT header at 0x%x:", (uintptr_t)mboot_ptr);
	debug_print(INFO, "Flags : 0x%x", mboot_ptr->flags);
	debug_print(INFO, "Mem Lo: 0x%x", mboot_ptr->mem_lower);
	debug_print(INFO, "Mem Hi: 0x%x", mboot_ptr->mem_upper);
	debug_print(INFO, "Boot d: 0x%x", mboot_ptr->boot_device);
	debug_print(INFO, "cmdlin: 0x%x", mboot_ptr->cmdline);
	debug_print(INFO, "Mods  : 0x%x", mboot_ptr->mods_count);
	debug_print(INFO, "Addr  : 0x%x", mboot_ptr->mods_addr);
	debug_print(INFO, "ELF n : 0x%x", mboot_ptr->num);
	debug_print(INFO, "ELF s : 0x%x", mboot_ptr->size);
	debug_print(INFO, "ELF a : 0x%x", mboot_ptr->addr);
	debug_print(INFO, "ELF h : 0x%x", mboot_ptr->shndx);
	debug_print(INFO, "MMap  : 0x%x", mboot_ptr->mmap_length);
	debug_print(INFO, "Addr  : 0x%x", mboot_ptr->mmap_addr);
	debug_print(INFO, "Drives: 0x%x", mboot_ptr->drives_length);
	debug_print(INFO, "Addr  : 0x%x", mboot_ptr->drives_addr);
	debug_print(INFO, "Config: 0x%x", mboot_ptr->config_table);
	debug_print(INFO, "Loader: 0x%x", mboot_ptr->boot_loader_name);
	debug_print(INFO, "APM   : 0x%x", mboot_ptr->apm_table);
	debug_print(INFO, "VBE Co: 0x%x", mboot_ptr->vbe_control_info);
	debug_print(INFO, "VBE Mo: 0x%x", mboot_ptr->vbe_mode_info);
	debug_print(INFO, "VBE In: 0x%x", mboot_ptr->vbe_mode);
	debug_print(INFO, "VBE se: 0x%x", mboot_ptr->vbe_interface_seg);
	debug_print(INFO, "VBE of: 0x%x", mboot_ptr->vbe_interface_off);
	debug_print(INFO, "VBE le: 0x%x", mboot_ptr->vbe_interface_len);
	if (mboot_ptr->flags & (1 << 2)) {
		debug_print(INFO, "Started with: %s", (char *)mboot_ptr->cmdline);
	}
	if (mboot_ptr->flags & (1 << 9)) {
		debug_print(INFO, "Booted from: %s", (char *)mboot_ptr->boot_loader_name);
	}
	if (mboot_ptr->flags & (1 << 0)) {
		debug_print(INFO, "%dkB lower memory", mboot_ptr->mem_lower);
		int mem_mb = mboot_ptr->mem_upper / 1024;
		debug_print(INFO, "%dkB higher memory (%dMB)", mboot_ptr->mem_upper, mem_mb);
	}
	if (mboot_ptr->flags & (1 << 3)) {
		debug_print(INFO, "Found %d module(s).", mboot_ptr->mods_count);
		if (mboot_ptr->mods_count > 0) {
			uint32_t i;
			for (i = 0; i < mboot_ptr->mods_count; ++i ) {
				uint32_t module_start = *((uint32_t*)mboot_ptr->mods_addr + 8 * i);
				uint32_t module_end   = *(uint32_t*)(mboot_ptr->mods_addr + 8 * i + 4);
				debug_print(INFO, "Module %d is at 0x%x:0x%x", i+1, module_start, module_end);
			}
		}
	}
}

