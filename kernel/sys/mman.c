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
#include <errno.h>
#include <sys/mman.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>
#include <kernel/string.h>

long generic_page_fault(uintptr_t addr, int flags) {

	return 0;
}

long mmap_sbrk(size_t size) {
	if (size & 0xFFF) return -EINVAL;
	volatile process_t * volatile proc = this_core->current_process->process;
	if (!proc) return -EINVAL;
	spin_lock(proc->image.lock);
	uintptr_t out = proc->image.heap;
	for (uintptr_t i = out; i < out + size; i += 0x1000) {
		union PML * page = mmu_get_page(i, MMU_GET_MAKE);
		if (page->bits.page != 0) {
			printf("odd, %#zx is already allocated?\n", i);
		}
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
	}
	proc->image.heap += size;
	spin_unlock(proc->image.lock);
	return (long)out;
}

extern void mmu_unmap_user(uintptr_t addr, size_t size);
long mmap_unmap(uintptr_t addr, size_t length) {
	volatile process_t * volatile proc = this_core->current_process->process;
	spin_lock(proc->image.lock);
	mmu_unmap_user(addr, length);
	spin_unlock(proc->image.lock);
	return 0;
}

long mmap_anon(uintptr_t addr, size_t length, int prot, int flags) {
	//dprintf("Map anonymous (zeroed on load) memory at %#zx[:%#zx]\n", addr, length);
	if (!(flags & MAP_FIXED)) return dprintf("must be fixed\n"), -ENOTSUP;
	if (addr & 0xFFF)   return dprintf("addr not aligned\n"), -EINVAL;
	if (length & 0xFFF) return dprintf("length not aligned\n"), -EINVAL;

	for (uintptr_t i = 0; i < length; i += 0x1000) {
		union PML * page = mmu_get_page(addr + i, MMU_GET_MAKE);
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
		memset((void*)(addr + i), 0, 0x1000);
	}

	if (prot & PROT_EXEC) {
		arch_clear_icache(addr, addr + length);
	}

	return addr;
}

long mmap_file(uintptr_t addr, size_t length, int prot, int flags, fs_node_t * file, off_t offset) {
	//dprintf("Map file from node %p[%#zx] to %#zx[:%#zx]\n",
	//	(void*)file, offset, addr, length);

	if (!(flags & MAP_FIXED)) return dprintf("must be fixed\n"), -ENOTSUP;
	if (addr & 0xFFF)   return dprintf("addr not aligned\n"), -EINVAL;
	if (length & 0xFFF) return dprintf("length not aligned\n"), -EINVAL;
	if (offset & 0xFFF) return dprintf("offset not aligned\n"), -EINVAL;

	for (uintptr_t i = 0; i < length; i += 0x1000) {
		union PML * page = mmu_get_page(addr + i, MMU_GET_MAKE);
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
		read_fs(file, offset + i, 0x1000, (void*)(addr + i));
	}

	if (prot & PROT_EXEC) {
		arch_clear_icache(addr, addr + length);
	}

	return addr;
}

