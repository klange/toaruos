/**
 * @file  kernel/arch/aarch64/traceback.c
 * @brief Kernel fault traceback generator.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2022 K. Lange
 */
#include <stdint.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/mmu.h>
#include <kernel/ksym.h>

#include <kernel/arch/aarch64/regs.h>

extern char end[];

static uintptr_t matching_symbol(uintptr_t ip, char ** name) {
	hashmap_t * symbols = ksym_get_map();
	uintptr_t best_match = 0;
	for (size_t i = 0; i < symbols->size; ++i) {
		hashmap_entry_t * x = symbols->entries[i];
		while (x) {
			void* sym_addr = x->value;
			char* sym_name = x->key;
			if ((uintptr_t)sym_addr < ip && (uintptr_t)sym_addr > best_match) {
				best_match = (uintptr_t)sym_addr;
				*name = sym_name;
			}
			x = x->next;
		}
	}
	return best_match;
}
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

static void dump_traceback(uintptr_t ip, uintptr_t bp, uintptr_t x30) {
	int depth = 0;
	int max_depth = 20;

	while (bp && ip && depth < max_depth) {
		dprintf(" 0x%016zx ", ip);
		#if 0
		if (ip >= 0xffffffff80000000UL) {
			char * name = NULL;
			struct LoadedModule * mod = find_module(ip, &name);
			if (mod) {
				dprintf("\a in module '%s', base address %#zx (offset %#zx)\n",
					name, mod->baseAddress, ip - mod->baseAddress);
			} else {
				dprintf("\a (unknown)\n");
			}
		#else
		if (0) {
		#endif
		} else if (ip <= 0x800000000000) {
			dprintf("\a in userspace\n");
		} else if (ip >= 0xffffffff80000000UL && ip <= (uintptr_t)&end) {
			/* Find symbol match */
			char * name;
			uintptr_t addr = matching_symbol(ip, &name);
			if (!addr) {
				dprintf("\a (no match)\n");
			} else {
				dprintf("\a %s+0x%zx\n", name, ip-addr);
			}
		} else {
			dprintf("\a (unknown)\n");
		}
		if (!validate_pointer(bp, sizeof(uintptr_t)) || !validate_pointer(bp + sizeof(uintptr_t), sizeof(uintptr_t))) {
			break;
		}
		if (depth == 0) {
			ip = x30;
		} else {
			ip = *(uintptr_t*)(bp + sizeof(uintptr_t));
			bp = *(uintptr_t*)(bp);
		}
		depth++;
	}
}

void aarch64_safe_dump_traceback(uintptr_t elr, struct regs * r) {
	dump_traceback(elr, r->x29, r->x30);
}


/* We need to pull the frame address from the caller or this isn't going work, but
 * gcc is going to warn that that is unsafe... */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-address"

/**
 * @brief Display a traceback from the current call context.
 */
void arch_dump_traceback(void) {
	dump_traceback((uintptr_t)arch_dump_traceback+1, (uintptr_t)__builtin_frame_address(1), (uintptr_t)__builtin_return_address(0));
}

#pragma GCC diagnostic pop
