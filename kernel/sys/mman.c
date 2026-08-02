/**
 * @file kernel/sys/mman.c
 * @brief Generic memory management functions
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdint.h>
#include <bits/errno.h>
#include <sys/mman.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>
#include <kernel/string.h>
#include <kernel/mman.h>

extern void mmu_unmap_user(uintptr_t addr, size_t size);

extern union PML * mmu_get_page_other_x(union PML * root, uintptr_t virtAddr, int flags);

enum fault_response mmap_fault_other(process_t * proc, uintptr_t addr, enum fault_code flags) {
	enum fault_response out = FAULT_RESPONSE_NO_MAPPING;

	spin_lock(proc->image.lock);
	int fail_on_retry = 0;

_retry: (void)0;
	for (memmap_t * maps = proc->thread.page_directory->mappings; maps; maps = maps->next) {
		if (addr >= maps->base && addr < maps->base + maps->length) {
			if (maps->prot == PROT_NONE) goto _fault_bad;

			/* Check if memory access would violate known mapping conditions. */
			if ((flags & FAULT_CODE_WRITE) && !(maps->prot & PROT_WRITE)) { out = FAULT_RESPONSE_BAD_WRITE; goto _fault_bad; }
			if ((flags & FAULT_CODE_READ) && !(maps->prot & PROT_READ))   { out = FAULT_RESPONSE_BAD_READ;  goto _fault_bad; }
			if ((flags & FAULT_CODE_INSTR) && !(maps->prot & PROT_EXEC))  { out = FAULT_RESPONSE_BAD_INSTR; goto _fault_bad; }

			size_t align_down = addr & ~0xFFF;
			size_t map_offset = align_down - maps->base;
			size_t map_fsoff  = maps->offset + map_offset;

			int mmu_flags = 0;
			if (maps->prot & PROT_WRITE) mmu_flags |= MMU_FLAG_WRITABLE;
			if (!(maps->prot & PROT_EXEC)) mmu_flags |= MMU_FLAG_NOEXECUTE;

			union PML * page = mmu_get_page_other_x(proc->thread.page_directory->directory, align_down, MMU_GET_MAKE);

			if (maps->file && maps->file->fault_map) {
				int ret = maps->file->fault_map(maps->file, page, map_fsoff, flags, maps->flags, maps->prot, &mmu_flags);

				if (ret == 0) {
					/* fault_map did something with the page, we should finish allocating it
					 * with the modified flags and return successfully. */
					mmu_frame_allocate(page, mmu_flags);
					if (proc == (process_t*)this_core->current_process) {
						mmu_invalidate(align_down);
						if (maps->prot & PROT_EXEC) arch_clear_icache(align_down, align_down + 0x1000);
					}
					spin_unlock(proc->image.lock);
					return FAULT_RESPONSE_RESUME;
				} else if (ret > 1) {
					goto _fault_bad;
				}

				/* Fault was deferred to normal code path. */
			}

			/* May have a shared CoW mapping, remove it.*/
			if ((flags & FAULT_CODE_WRITE) && maps->file) {
				if (page->bits.present && page->bits.page && (page->bits.mmap_shared & 1)) {
					page->bits.page = 0;
					page->bits.mmap_shared = 0;
				}
			}

			mmu_frame_allocate(page, mmu_flags);

			char * page_back = mmu_map_from_physical((uintptr_t)page->bits.page << 12);
			if (maps->file) {
				ssize_t r = read_fs(maps->file, map_fsoff, 0x1000, (void*)page_back);
				if (r >= 0 && r < 0x1000) {
					memset((void*)(page_back + r), 0, 0x1000 - r);
				}
			} else {
				memset((void*)(page_back), 0, 0x1000);
			}
			mmu_flush(page_back);
			spin_unlock(proc->image.lock);

			return FAULT_RESPONSE_RESUME;
		}
	}

	if (!fail_on_retry && addr < proc->image.userstack && addr >= proc->image.userstack - 0x10000) {
		size_t align_down = addr & ~0xFFF;
		do_mmap(align_down, proc->image.userstack - align_down, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_FIXED, NULL, 0);
		proc->image.userstack = align_down;
		fail_on_retry = 1;
		goto _retry;
	}

_fault_bad:
	spin_unlock(proc->image.lock);
	return out;
}

enum fault_response generic_page_fault(uintptr_t addr, enum fault_code flags, struct regs * r) {
	return mmap_fault_other(this_core->current_process->process, addr, flags);
}

long mmap_sbrk(size_t size) {
	return do_mmap(0, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, NULL, 0);
}

static int map_overlaps(memmap_t * map, uintptr_t addr, intptr_t length) {
	return (map->base < addr + length && addr < map->base + map->length);
}

static void unmap_segments_locked(uintptr_t addr, intptr_t length, process_t * proc) {
	memmap_t * prev = NULL;
	memmap_t * next = NULL;
	for (memmap_t * maps = proc->thread.page_directory->mappings; maps; maps = next) {
		next = maps->next;
		if (map_overlaps(maps, addr, length)) {
			uintptr_t nend = addr + length;
			uintptr_t oend = maps->base + maps->length;

			if (addr <= maps->base && nend >= oend) {
				if (!prev) this_core->current_process->thread.page_directory->mappings = maps->next;
				else prev->next = maps->next;
				if (maps->next) maps->next->prev = maps->prev;
				if (maps->file) close_fs(maps->file);
				free(maps);
				continue;
			} else if (addr <= maps->base && nend < oend) {
				size_t into = nend - maps->base;
				if (maps->file) maps->offset = maps->offset + into;
				maps->base = nend;
				maps->length = oend - nend;
			} else if (addr > maps->base && nend >= oend) {
				maps->length = addr - maps->base;
			} else if (addr > maps->base && nend < oend) {
				maps->length = addr - maps->base;
				memmap_t * split = calloc(1, sizeof(memmap_t));

				size_t into = nend - maps->base;
				split->base   = nend;
				split->length = oend - nend;
				split->owner = maps->owner;
				split->prot = maps->prot;
				split->flags = maps->flags;
				split->file = maps->file;
				if (maps->file) {
					split->offset = maps->offset + into;
					open_fs(split->file, 0);
				}

				split->next = maps->next;
				if (split->next) split->next->prev = split;
				split->prev = maps;
				maps->next = split;
			}
		}

		prev = maps;
	}
	mmu_unmap_user(addr, length);
}

long mmap_unmap(uintptr_t addr, size_t length) {
	process_t * proc = this_core->current_process->process;
	spin_lock(proc->image.lock);
	unmap_segments_locked(addr, length, proc);
	spin_unlock(proc->image.lock);
	return 0;
}

static void sanity_check(union PML * page) {
#if defined(__x86_64__)
	if (page->bits.cow_pending) {
		arch_fatal_prepare();
		dprintf("mmap: trying to overwrite existing cow page?\n");
		arch_dump_traceback();
		arch_fatal();
	}
#endif
}

static void insert_mapping(uintptr_t addr, intptr_t length, int prot, int flags, fs_node_t * node, off_t offset) {
	process_t * proc = this_core->current_process->process;
	unmap_segments_locked(addr, length, proc);

	flags &= ~(MAP_FIXED);

	memmap_t * prev = NULL;
	memmap_t * next = this_core->current_process->thread.page_directory->mappings;
	for (memmap_t * maps = this_core->current_process->thread.page_directory->mappings; maps; maps = maps->next) {
		if (maps->base > addr) break;
		next = maps->next;
		prev = maps;
	}

	if (prev && prev->base + prev->length == addr && prev->flags == flags && prev->prot == prot && prev->file == node && (!node || (prev->offset + length == offset))) {
		/* merge backwards */
		prev->length += length;

		if (next && next->base == addr + length && next->flags == flags && next->prot == prot && next->file == node && (!node || (prev->offset + prev->length == next->offset))) {
			/* Also merge forward */
			prev->length += next->length;
			prev->next = next->next;
			if (next->next) {
				next->next->prev = prev;
			}

			if (next->file) close_fs(next->file);
			free(next);
		}
	} else if (next && next->base == addr + length && next->flags == flags && next->prot == prot && next->file == node && (!node || (offset + length == next->offset))) {
		/* Merge only forward */
		next->base = addr;
		next->length += length;
		if (node) next->offset = offset;
	} else {
		memmap_t * new_mapping = calloc(1, sizeof(memmap_t));
		new_mapping->base = addr;
		new_mapping->length = length;
		new_mapping->prot = prot;
		new_mapping->flags = flags;
		new_mapping->file = node;
		if (node) {
			new_mapping->offset = offset;
			open_fs(node, 0);
		}

		new_mapping->owner = this_core->current_process->thread.page_directory;

		if (!prev) {
			this_core->current_process->thread.page_directory->mappings = new_mapping;
		} else {
			prev->next = new_mapping;
			new_mapping->prev = prev;
		}
		new_mapping->next = next;
		if (next) next->prev = new_mapping;
	}
}

static uintptr_t find_good_spot(process_t * proc, size_t length) {
	/* First try for a perfect fit */
	for (memmap_t * maps = proc->thread.page_directory->mappings; maps; maps = maps->next) {
		memmap_t * next = maps->next;
		if (next && next->base == maps->base + maps->length + length) {
			return maps->base + maps->length;
		}
	}
	/* Then try for... a fit. */
	for (memmap_t * maps = proc->thread.page_directory->mappings; maps; maps = maps->next) {
		memmap_t * next = maps->next;
		if (next && next->base >= maps->base + maps->length + length) {
			return maps->base + maps->length;
		}
	}
	/* Then go back to the end-of-heap pointer. */
	uintptr_t addr = proc->image.heap;

	for (memmap_t * maps = proc->thread.page_directory->mappings; maps; maps = maps->next) {
		if (map_overlaps(maps, addr, length)) {
			addr = maps->base + maps->length;
			continue;
		}
	}

	proc->image.heap = addr + length;
	return addr;
}

long do_mmap(uintptr_t addr, size_t length, int prot, int flags, fs_node_t * file, off_t offset) {
	process_t * proc = this_core->current_process->process;

	/* Address must be aligned */
	if (addr & 0xFFF) return -EINVAL;

	/* Length must be aligned */
	if (length & 0xFFF) return -EINVAL;

	/* Length must not be 0. */
	if (length == 0) return -EINVAL;

	/* Length must not be too weird. */
	if (length > 0x800000000) return -ENOMEM;

	/* Fixed mappings must be in the lower half. */
	if ((flags & MAP_FIXED) && addr > 0x800000000000UL - length) return -EINVAL;

	if (file) {
		if (flags & MAP_ANONYMOUS) return -EINVAL;
		if (offset & 0xFFF) return -EINVAL;
		if ((flags & MAP_SHARED) && !file->fault_map) return -EINVAL;
	} else {
		if (!(flags & MAP_ANONYMOUS)) return -EINVAL;
		if (flags & MAP_SHARED) return -ENOTSUP;
		flags |= MAP_ANONYMOUS; /* just in case */
		offset = 0;
	}

	spin_lock(proc->image.lock);
	if (!(flags & MAP_FIXED)) addr = find_good_spot(proc, length);
	insert_mapping(addr, length, prot, flags, file, offset);
	spin_unlock(proc->image.lock);
	return addr;
}

