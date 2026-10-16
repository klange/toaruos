/**
 * @file  kernel/misc/traceback.c
 * @brief Common traceback-related functions.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2026 K. Lange
 */
#include <kernel/process.h>
#include <kernel/printf.h>
#include <kernel/mmu.h>
#include <kernel/module.h>
#include <kernel/ksym.h>
#include <kernel/traceback.h>

extern char end[];

static int validate_pointer(uintptr_t base, size_t size) {
	uintptr_t end  = size ? (base + (size - 1)) : base;
	uintptr_t page_base = base >> 12;
	uintptr_t page_end  =  end >> 12;
	for (uintptr_t page = page_base; page <= page_end; ++page) {
		if ((page & 0xffff800000000) != 0 && (page & 0xffff800000000) != 0xffff800000000) return 0;
		union PML * page_entry = mmu_get_page_other(this_core->current_process->thread.page_directory->directory, page << 12);
		if (!page_entry) return 0;
		if (!page_entry->bits.present) return 0;
	}
	return 1;
}

int traceback_print_line(uintptr_t ip, uintptr_t bp) {
	dprintf(" 0x%016zx ", ip);
	if (ip <= 0x800000000000) {
		for (memmap_t * maps = this_core->current_process->thread.page_directory->mappings; maps; maps = maps->next) {
			if (ip >= maps->base && ip < maps->base + maps->length) {
				if (maps->file) {
					dprintf("\a %s+%#zx", maps->file->path->chars,
						maps->offset + ip - maps->base);
				}
				break;
			}
		}
		dprintf("\a in userspace\n"); /* TODO loaded object */
	} else if (ip >= 0xffffffff80000000UL) {
		/* Find symbol match */
		char * name;
		uintptr_t addr = ksym_closest(ip, &name);
		struct LoadedModule * mod = modules_find_module(ip, &name);
		if (addr && (!mod || addr >= mod->baseAddress)) {
			dprintf("\a %s+0x%zx", name, ip-addr);
		}
		if (mod) {
			dprintf("\a in module '%s', base address %#zx (offset %#zx)",
				name, mod->baseAddress, ip - mod->baseAddress);
		}
		dprintf("\a\n");
	} else {
		dprintf("\a (unknown)\n");
	}
	return (!validate_pointer(bp, sizeof(uintptr_t)) || !validate_pointer(bp + sizeof(uintptr_t), sizeof(uintptr_t)));
}

