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

long generic_page_fault(uintptr_t addr, int flags) {

	return 0;
}

long mmap_sbrk(size_t size) {
	return mmap_anon(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE);
}

extern void mmu_unmap_user(uintptr_t addr, size_t size);
long mmap_unmap(uintptr_t addr, size_t length) {
	process_t * proc = this_core->current_process->process;
	spin_lock(proc->image.lock);
	mmu_unmap_user(addr, length);
	spin_unlock(proc->image.lock);
	return 0;
}

static long mmap_common_checks(uintptr_t addr, size_t length, int prot, int flags) {
	if (!(flags & MAP_PRIVATE)) return dprintf("must be private\n"), -ENOTSUP;
	if (addr & 0xFFF)   return dprintf("addr not aligned\n"), -EINVAL;
	if (length & 0xFFF) return dprintf("length not aligned\n"), -EINVAL;
	if (length == 0) return -EINVAL;

	if (length > 0x800000000) return -ENOMEM;

	if (flags & MAP_FIXED) {
#ifdef __x86_64__
		/* Deal with our dumb low mapping of the x86-64 kernel. */
		extern char end[];
		uintptr_t endPtr = ((uintptr_t)&end + 0xFFF) & 0xFFF;
		if (addr < endPtr) return -EINVAL;
#endif

		/* Dumb stuff */
		if (addr > 0x800000000000UL) return -EINVAL;
		if (addr + length > 0x800000000000UL) return -EINVAL;
	}

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

long mmap_anon(uintptr_t addr, size_t length, int prot, int flags) {
	//dprintf("mmap(%#zx, %zu, %d, %d | MAP_ANONYMOUS, -1, 0);\n", addr, length, prot, flags);
	process_t * proc = this_core->current_process->process;

	long ret;
	if ((ret = mmap_common_checks(addr, length, prot, flags))) return ret;

	if (!(flags & MAP_FIXED)) {
		spin_lock(proc->image.lock);
		addr = proc->image.heap;
		proc->image.heap += length;
		spin_unlock(proc->image.lock);
	}

	/* A mapping with PROT_NONE does... nothing?
	 * We need to at least mark the mapping as non-present or something. */
	if (prot & PROT_NONE) return addr;

	int mmu_flags = 0;
	if (prot & PROT_WRITE) mmu_flags |= MMU_FLAG_WRITABLE;
	if (!(prot & PROT_EXEC)) mmu_flags |= MMU_FLAG_NOEXECUTE;

	for (uintptr_t i = 0; i < length; i += 0x1000) {
		union PML * page = mmu_get_page(addr + i, MMU_GET_MAKE);
		sanity_check(page);
		mmu_frame_allocate(page, mmu_flags);
		char * page_back = mmu_map_from_physical((uintptr_t)page->bits.page << 12);
		memset((void*)(page_back), 0, 0x1000);
		mmu_flush(page_back);
	}

	if (prot & PROT_EXEC) {
		arch_clear_icache(addr, addr + length);
	}

	return addr;
}

long mmap_file(uintptr_t addr, size_t length, int prot, int flags, fs_node_t * file, off_t offset) {
	process_t * proc = this_core->current_process->process;
	//dprintf("mmap(%#zx, %zu, %d, %d, *%p, %#zx); pid=%d\n", addr, length, prot, flags, (void*)file, offset, proc->id);

	long ret;
	if ((ret = mmap_common_checks(addr, length, prot, flags))) return ret;

	if (offset & 0xFFF) return dprintf("offset not aligned\n"), -EINVAL;

	if (!(flags & MAP_FIXED)) {
		spin_lock(proc->image.lock);
		addr = proc->image.heap;
		proc->image.heap += length;
		spin_unlock(proc->image.lock);
	}

	/* A mapping with PROT_NONE does... nothing?
	 * We need to at least mark the mapping as non-present or something. */
	if (prot & PROT_NONE) return addr;

	int mmu_flags = 0;
	if (prot & PROT_WRITE) mmu_flags |= MMU_FLAG_WRITABLE;
	if (!(prot & PROT_EXEC)) mmu_flags |= MMU_FLAG_NOEXECUTE;

	for (uintptr_t i = 0; i < length; i += 0x1000) {
		union PML * page = mmu_get_page(addr + i, MMU_GET_MAKE);
		sanity_check(page);
		mmu_frame_allocate(page, mmu_flags);

		char * page_back = mmu_map_from_physical((uintptr_t)page->bits.page << 12);
		ssize_t r = read_fs(file, offset + i, 0x1000, (void*)page_back);
		if (r >= 0 && r < 0x1000) {
			memset((void*)(page_back + r), 0, 0x1000 - r);
		}

		mmu_flush(page_back);
	}

	if (prot & PROT_EXEC) {
		arch_clear_icache(addr, addr + length);
	}

	return addr;
}

