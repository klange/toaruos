/**
 * @file  kernel/arch/x86_64/mmu.c
 * @brief Memory management facilities for x86-64
 *
 * Frame allocation and mapping routines for x86-64.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdint.h>
#include <kernel/assert.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/misc.h>
#include <kernel/mmu.h>
#include <kernel/arch/x86_64/pml.h>

extern void arch_tlb_shootdown(uintptr_t);

/**
 * bitmap page allocator for 4KiB pages
 */
static volatile uint32_t *frames;
static size_t nframes;
static size_t total_memory = 0;
static size_t unavailable_memory = 0;
static uint8_t * mem_refcounts = NULL;

#define PAGE_SHIFT     12
#define PAGE_SIZE      0x1000UL
#define PAGE_SIZE_MASK 0xFFFFffffFFFFf000UL
#define PAGE_LOW_MASK  0x0000000000000FFFUL

#define LARGE_PAGE_SIZE 0x200000UL

#define   USER_PML_ACCESS 0x07
#define KERNEL_PML_ACCESS 0x03
#define    LARGE_PAGE_BIT 0x80

#define PDP_MASK 0x3fffffffUL
#define  PD_MASK 0x1fffffUL
#define  PT_MASK PAGE_LOW_MASK
#define ENTRY_MASK 0x1FF

#define PHYS_MASK 0x7fffffffffUL
#define CANONICAL_MASK 0xFFFFffffFFFFUL

#define INDEX_FROM_BIT(b)  ((b) >> 5)
#define OFFSET_FROM_BIT(b) ((b) & 0x1F)

/**
 * @brief Mark a physical page frame as in use.
 *
 * Sets the bitmap allocator bit for a frame.
 *
 * @param frame_addr Address of the frame (not index!)
 */
void mmu_frame_set(uintptr_t frame_addr) {
	/* If the frame is within bounds... */
	if (frame_addr < nframes * PAGE_SIZE) {
		uint64_t frame  = frame_addr >> 12;
		uint64_t index  = INDEX_FROM_BIT(frame);
		uint32_t offset = OFFSET_FROM_BIT(frame);
		frames[index]  |= ((uint32_t)1 << offset);
		asm ("" ::: "memory");
	}
}

static uintptr_t lowest_available = 0;

/**
 * @brief Mark a physical page frame as available.
 *
 * Clears the bitmap allocator bit for a frame.
 *
 * @param frame_addr Address of the frame (not index!)
 */
void mmu_frame_clear(uintptr_t frame_addr) {
	/* If the frame is within bounds... */
	if (frame_addr < nframes * PAGE_SIZE) {
		uint64_t frame  = frame_addr >> PAGE_SHIFT;
		uint64_t index  = INDEX_FROM_BIT(frame);
		uint32_t offset = OFFSET_FROM_BIT(frame);
		frames[index]  &= ~((uint32_t)1 << offset);
		asm ("" ::: "memory");
		if (frame < lowest_available) lowest_available = frame;
	}
}

/**
 * @brief Determine if a physical page is available for use.
 *
 * @param frame_addr Address of the frame (not index!)
 * @returns 0 if available, 1 otherwise.
 */
int mmu_frame_test(uintptr_t frame_addr) {
	if (!(frame_addr < nframes * PAGE_SIZE)) return 1;
	uint64_t frame  = frame_addr >> PAGE_SHIFT;
	uint64_t index  = INDEX_FROM_BIT(frame);
	uint32_t offset = OFFSET_FROM_BIT(frame);
	asm ("" ::: "memory");
	return !!(frames[index] & ((uint32_t)1 << offset));
}

static spin_lock_t frame_alloc_lock = { 0 };
static spin_lock_t kheap_lock = { 0 };
static spin_lock_t mmio_space_lock = { 0 };
static spin_lock_t module_space_lock = { 0 };

void mmu_frame_release(uintptr_t frame_addr) {
	spin_lock(frame_alloc_lock);
	mmu_frame_clear(frame_addr);
	spin_unlock(frame_alloc_lock);
}

/**
 * @brief Find the first range of @p n contiguous frames.
 *
 * If a large enough region could not be found, results are fatal.
 */
uintptr_t mmu_first_n_frames(int n) {
	for (uint64_t i = 0; i < nframes * PAGE_SIZE; i += PAGE_SIZE) {
		int bad = 0;
		for (int j = 0; j < n; ++j) {
			if (mmu_frame_test(i + PAGE_SIZE * j)) {
				bad = j + 1;
			}
		}
		if (!bad) {
			return i / PAGE_SIZE;
		}
	}

	arch_fatal_prepare();
	dprintf("Failed to allocate %d contiguous frames.\n", n);
	arch_dump_traceback();
	arch_fatal();
	return (uintptr_t)-1;
}

/**
 * @brief Find the first available frame from the bitmap.
 */
uintptr_t mmu_first_frame(void) {
	uintptr_t i, j;
	for (i = INDEX_FROM_BIT(lowest_available); i < INDEX_FROM_BIT(nframes); ++i) {
		if (frames[i] != (uint32_t)-1) {
			for (j = 0; j < (sizeof(uint32_t)*8); ++j) {
				uint32_t testFrame = (uint32_t)1 << j;
				if (!(frames[i] & testFrame)) {
					uintptr_t out = (i << 5) + j;
					lowest_available = out + 1;
					return out;
				}
			}
		}
	}

	arch_fatal_prepare();
	dprintf("Out of memory.\n");
	arch_dump_traceback();
	arch_fatal();
	return (uintptr_t)-1;
}

/**
 * @brief Set the flags for a page, and allocate a frame for it if needed.
 *
 * Sets the page bits based on the the value of @p flags.
 * If @p page->bits.page is unset, a new frame will be allocated.
 */
void mmu_frame_allocate(union PML * page, unsigned int flags) {
	if (page->bits.page == 0) {
		spin_lock(frame_alloc_lock);
		uintptr_t index = mmu_first_frame();
		mmu_frame_set(index << PAGE_SHIFT);
		page->bits.page     = index;
		spin_unlock(frame_alloc_lock);
	}
	page->bits.size     = 0;
	page->bits.present  = 1;
	page->bits.writable = (flags & MMU_FLAG_WRITABLE) ? 1 : 0;
	page->bits.user     = (flags & MMU_FLAG_KERNEL)   ? 0 : 1;
	page->bits.nocache  = (flags & MMU_FLAG_NOCACHE)  ? 1 : 0;
	page->bits.writethrough  = (flags & MMU_FLAG_WRITETHROUGH)  ? 1 : 0;
	page->bits.size     = (flags & MMU_FLAG_SPEC) ? 1 : 0;
	page->bits.nx       = (flags & MMU_FLAG_NOEXECUTE) ? 1 : 0;
}

/**
 * @brief Map the given page to the requested physical address.
 */
void mmu_frame_map_address(union PML * page, unsigned int flags, uintptr_t physAddr) {
	mmu_frame_set(physAddr);
	page->bits.page = physAddr >> PAGE_SHIFT;
	mmu_frame_allocate(page, flags);
}

/* Initial memory maps loaded by boostrap */
#define _pagemap __attribute__((aligned(PAGE_SIZE))) = {0}
union PML init_page_region[3][512] _pagemap;
union PML high_base_pml[512] _pagemap;
union PML heap_base_pml[512] _pagemap;
union PML heap_base_pd[512] _pagemap;
union PML heap_base_pt[512*3] _pagemap;
union PML low_base_pmls[34][512] _pagemap;
union PML twom_high_pds[64][512] _pagemap;

/**
 * @brief Maps a frame address to a virtual address.
 *
 * Returns the virtual address within the general-purpose
 * identity mapping region for the given physical frame address.
 * This address is not suitable for some operations, such as MMIO.
 */
void * mmu_map_from_physical(uintptr_t frameaddress) {
	return (void*)(frameaddress | HIGH_MAP_REGION);
}

union PML * mmu_get_page_other(union PML * root, uintptr_t virtAddr) {
	uintptr_t realBits = virtAddr & CANONICAL_MASK;
	uintptr_t pageAddr = realBits >> PAGE_SHIFT;
	unsigned int pml4_entry = (pageAddr >> 27) & ENTRY_MASK;
	unsigned int pdp_entry  = (pageAddr >> 18) & ENTRY_MASK;
	unsigned int pd_entry   = (pageAddr >> 9)  & ENTRY_MASK;
	unsigned int pt_entry   = (pageAddr) & ENTRY_MASK;

	/* Get the PML4 entry for this address */
	if (!root[pml4_entry].bits.present) {
		return NULL;
	}

	union PML * pdp = mmu_map_from_physical((uintptr_t)root[pml4_entry].bits.page << PAGE_SHIFT);

	if (!pdp[pdp_entry].bits.present) {
		return NULL;
	}

	if (pdp[pdp_entry].bits.size) {
		return NULL;
	}

	union PML * pd = mmu_map_from_physical((uintptr_t)pdp[pdp_entry].bits.page << PAGE_SHIFT);

	if (!pd[pd_entry].bits.present) {
		return NULL;
	}

	if (pd[pd_entry].bits.size) {
		return NULL;
	}

	union PML * pt = mmu_map_from_physical((uintptr_t)pd[pd_entry].bits.page << PAGE_SHIFT);
	return (union PML *)&pt[pt_entry];
}

/**
 * @brief Find the physical address at a given virtual address.
 *
 * Calculates the physical address of the page backing the virtual
 * address @p virtAddr. If no page is mapped, a negative value
 * is returned indicating which level of the page directory is
 * unmapped from -1 (no PDP) to -4 (page not present in table).
 */
uintptr_t mmu_map_to_physical(union PML * root, uintptr_t virtAddr) {
	uintptr_t realBits = virtAddr & CANONICAL_MASK;
	uintptr_t pageAddr = realBits >> PAGE_SHIFT;
	unsigned int pml4_entry = (pageAddr >> 27) & ENTRY_MASK;
	unsigned int pdp_entry  = (pageAddr >> 18) & ENTRY_MASK;
	unsigned int pd_entry   = (pageAddr >> 9)  & ENTRY_MASK;
	unsigned int pt_entry   = (pageAddr) & ENTRY_MASK;

	/* Get the PML4 entry for this address */
	if (!root[pml4_entry].bits.present) return (uintptr_t)-1;

	union PML * pdp = mmu_map_from_physical((uintptr_t)root[pml4_entry].bits.page << PAGE_SHIFT);

	if (!pdp[pdp_entry].bits.present) return (uintptr_t)-2;
	if (pdp[pdp_entry].bits.size) return ((uintptr_t)pdp[pdp_entry].bits.page << PAGE_SHIFT) | (virtAddr & PDP_MASK);

	union PML * pd = mmu_map_from_physical((uintptr_t)pdp[pdp_entry].bits.page << PAGE_SHIFT);

	if (!pd[pd_entry].bits.present) return (uintptr_t)-3;
	if (pd[pd_entry].bits.size) return ((uintptr_t)pd[pd_entry].bits.page << PAGE_SHIFT) | (virtAddr & PD_MASK);

	union PML * pt = mmu_map_from_physical((uintptr_t)pd[pd_entry].bits.page << PAGE_SHIFT);

	if (!pt[pt_entry].bits.present) return (uintptr_t)-4;
	return ((uintptr_t)pt[pt_entry].bits.page << PAGE_SHIFT) | (virtAddr & PT_MASK);
}

/**
 * @brief Obtain the page entry for a virtual address.
 *
 * Digs into the current page directory to obtain the page entry
 * for a requested address @p virtAddr. If new intermediary directories
 * need to be allocated and @p flags has @c MMU_GET_MAKE set, they
 * will be allocated with the user access bits set. Otherwise,
 * NULL will be returned. If the requested virtual address is within
 * a large page, NULL will be returned.
 *
 * @param virtAddr Canonical virtual address offset.
 * @param flags See @c MMU_GET_MAKE
 * @returns the requested page entry, or NULL if doing so required allocating
 *          an intermediary paging level and @p flags did not have @c MMU_GET_MAKE set.
 */
union PML * mmu_get_page(uintptr_t virtAddr, int flags) {
	uintptr_t realBits = virtAddr & CANONICAL_MASK;
	uintptr_t pageAddr = realBits >> PAGE_SHIFT;
	unsigned int pml4_entry = (pageAddr >> 27) & ENTRY_MASK;
	unsigned int pdp_entry  = (pageAddr >> 18) & ENTRY_MASK;
	unsigned int pd_entry   = (pageAddr >> 9)  & ENTRY_MASK;
	unsigned int pt_entry   = (pageAddr) & ENTRY_MASK;

	union PML * root = this_core->current_pml;

	/* Get the PML4 entry for this address */
	if (!root[pml4_entry].bits.present) {
		if (!(flags & MMU_GET_MAKE)) goto _noentry;
		spin_lock(frame_alloc_lock);
		uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
		mmu_frame_set(newPage);
		spin_unlock(frame_alloc_lock);
		/* zero it */
		memset(mmu_map_from_physical(newPage), 0, PAGE_SIZE);
		root[pml4_entry].raw = (newPage) | USER_PML_ACCESS;
	}

	union PML * pdp = mmu_map_from_physical((uintptr_t)root[pml4_entry].bits.page << PAGE_SHIFT);

	if (!pdp[pdp_entry].bits.present) {
		if (!(flags & MMU_GET_MAKE)) goto _noentry;
		spin_lock(frame_alloc_lock);
		uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
		mmu_frame_set(newPage);
		spin_unlock(frame_alloc_lock);
		/* zero it */
		memset(mmu_map_from_physical(newPage), 0, PAGE_SIZE);
		pdp[pdp_entry].raw = (newPage) | USER_PML_ACCESS;
	}

	if (pdp[pdp_entry].bits.size) {
		printf("Warning: Tried to get page for a 1GiB page!\n");
		return NULL;
	}

	union PML * pd = mmu_map_from_physical((uintptr_t)pdp[pdp_entry].bits.page << PAGE_SHIFT);

	if (!pd[pd_entry].bits.present) {
		if (!(flags & MMU_GET_MAKE)) goto _noentry;
		spin_lock(frame_alloc_lock);
		uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
		mmu_frame_set(newPage);
		spin_unlock(frame_alloc_lock);
		/* zero it */
		memset(mmu_map_from_physical(newPage), 0, PAGE_SIZE);
		pd[pd_entry].raw = (newPage) | USER_PML_ACCESS;
	}

	if (pd[pd_entry].bits.size) {
		printf("Warning: Tried to get page for a 2MiB page!\n");
		return NULL;
	}

	union PML * pt = mmu_map_from_physical((uintptr_t)pd[pd_entry].bits.page << PAGE_SHIFT);
	return (union PML *)&pt[pt_entry];

_noentry:
	printf("no entry for requested page\n");
	return NULL;
}

/**
 * @brief Increment the reference count for a physical page of memory.
 *
 * We allow up to 255 references to a page, so that we can track individual
 * page reference counts in a big @c uint8_t array. If there are already
 * that many references (that's a lot of forks!) we give up and do a regular
 * copy of the page and the new copy is writable.
 *
 * @param frame Physical page index
 * @returns 1 if there are already too many references to this page, 0 otherwise.
 */
int refcount_inc(uintptr_t frame) {
	if (frame >= nframes) {
		arch_fatal_prepare();
		dprintf("%zu (inc, bad frame)\n", frame);
		arch_dump_traceback();
		arch_fatal();
	}
	if (mem_refcounts[frame] == 255) return 1;
	mem_refcounts[frame]++;
	return 0;
}

/**
 * @brief Decrement the reference count for a physical page of memory.
 *
 * Panics if @p frame is invalid or has a zero reference count.
 *
 * @param frame Physical page index
 * @returns the resulting reference count.
 */
uint8_t refcount_dec(uintptr_t frame) {
	if (frame >= nframes) {
		arch_fatal_prepare();
		dprintf("%zu (dec, bad frame)\n", frame);
		arch_dump_traceback();
		arch_fatal();
	}
	if (mem_refcounts[frame] == 0) {
		arch_fatal_prepare();
		dprintf("%zu (dec, frame has no references)\n", frame);
		arch_dump_traceback();
		arch_fatal();
	}
	mem_refcounts[frame]--;
	return mem_refcounts[frame];
}

/**
 * @brief Handle user pages in mmu_clone
 *
 * Copies and updates reference counts for pages across forks.
 * If a page was writable in the source directory, it will be marked
 * read-only and have reference counts initialized for COW.
 *
 * If a page was already read-only, its reference count will
 * be incremented for the new directory.
 *
 * @param pt_in Existing page table.
 * @param pt_out New directory's page table.
 * @param l Index into both page tables for this page.
 * @param address Virtual address being referenced.
 * @returns 0, generally
 */
int copy_page_maybe(union PML * pt_in, union PML * pt_out, size_t l, uintptr_t address) {
	/* Can we cow the current page? */
	spin_lock(frame_alloc_lock);

	/* Is the page writable? */
	if (pt_in[l].bits.writable) {
		/* Then we need to initialize the refcounts */
		if (mem_refcounts[pt_in[l].bits.page] != 0) {
			arch_fatal_prepare();
			dprintf("%#zx (page=%u) refcount = %u\n",
				address, pt_in[l].bits.page, mem_refcounts[pt_in[l].bits.page]);
			arch_dump_traceback();
			arch_fatal();
			return 1;
		}
		mem_refcounts[pt_in[l].bits.page] = 2;
		pt_in[l].bits.writable = 0;
		pt_in[l].bits.cow_pending = 1;
		pt_out[l].raw = pt_in[l].raw;
		asm ("" ::: "memory");
		mmu_invalidate(address);
		spin_unlock(frame_alloc_lock);
		return 0;
	}

	/* Can we make a new reference? */
	if (refcount_inc(pt_in[l].bits.page)) {
		/* There are too many references to fit in our refcount table, so just make a new page. */
		char * page_in = mmu_map_from_physical((uintptr_t)pt_in[l].bits.page << PAGE_SHIFT);
		uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
		char * page_out = mmu_map_from_physical(newPage);
		memcpy(page_out,page_in,PAGE_SIZE);
		pt_out[l].raw = 0;
		pt_out[l].bits.present = 1;
		pt_out[l].bits.user = 1;
		pt_out[l].bits.page = newPage >> PAGE_SHIFT;
		pt_out[l].bits.writable = 1;
		pt_out[l].bits.cow_pending = 0;
		asm ("" ::: "memory");
	} else {
		pt_out[l].raw = pt_in[l].raw;
	}

	spin_unlock(frame_alloc_lock);
	return 0;
}

/**
 * @brief When freeing a directory, handle individual user pages.
 *
 * If @p pt_in references a writable user page, we know we can
 * free it immediately as it is the only reference to that page.
 *
 * Otherwise, we need to decrement the reference counts for read-only
 * pages, as they are shared COW entries. Only if this was the last
 * reference (refcount drops to 0) can we then proceed to free the
 * underlying page.
 *
 * @param pt_in Start of page table
 * @param l Offset into page table for this page
 * @param address Virtual address being freed (was used for debugging)
 * @returns 0, generally
 */
int free_page_maybe(union PML * pt_in, size_t l, uintptr_t address) {
	if (pt_in[l].bits.writable) {
		assert(mem_refcounts[pt_in[l].bits.page] == 0);
		mmu_frame_clear((uintptr_t)pt_in[l].bits.page << PAGE_SHIFT);
		return 0;
	}

	/* No more references */
	if (refcount_dec(pt_in[l].bits.page) == 0) {
		mmu_frame_clear((uintptr_t)pt_in[l].bits.page << PAGE_SHIFT);
	}

	return 0;
}

/**
 * @brief Create a new address space with the same contents of an existing one.
 *
 * Allocates all of the necessary intermediary directory levels for a new address space
 * and also copies data from the existing address space.
 *
 * TODO: This doesn't do any CoW and it's kinda complicated.
 *
 * @param from The directory to clone, or NULL to clone the kernel map.
 * @returns a pointer to the new page directory, suitable for mapping to a physical address.
 */
union PML * mmu_clone(union PML * from) {
	/* Clone the current PMLs... */
	if (!from) from = this_core->current_pml;

	/* First get a page for ourselves. */
	spin_lock(frame_alloc_lock);
	uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
	mmu_frame_set(newPage);
	spin_unlock(frame_alloc_lock);
	union PML * pml4_out = mmu_map_from_physical(newPage);

	/* Zero bottom half */
	memset(&pml4_out[0], 0, 256 * sizeof(union PML));

	/* Copy top half */
	memcpy(&pml4_out[256], &from[256], 256 * sizeof(union PML));

	/* Copy PDPs */
	for (size_t i = 0; i < 256; ++i) {
		if (from[i].bits.present) {
			union PML * pdp_in = mmu_map_from_physical((uintptr_t)from[i].bits.page << PAGE_SHIFT);
			spin_lock(frame_alloc_lock);
			uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
			mmu_frame_set(newPage);
			spin_unlock(frame_alloc_lock);
			union PML * pdp_out = mmu_map_from_physical(newPage);
			memset(pdp_out, 0, 512 * sizeof(union PML));
			pml4_out[i].raw = (newPage) | USER_PML_ACCESS;

			/* Copy the PDs */
			for (size_t j = 0; j < 512; ++j) {
				if (pdp_in[j].bits.present) {
					union PML * pd_in = mmu_map_from_physical((uintptr_t)pdp_in[j].bits.page << PAGE_SHIFT);
					spin_lock(frame_alloc_lock);
					uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
					mmu_frame_set(newPage);
					spin_unlock(frame_alloc_lock);
					union PML * pd_out = mmu_map_from_physical(newPage);
					memset(pd_out, 0, 512 * sizeof(union PML));
					pdp_out[j].raw = (newPage) | USER_PML_ACCESS;

					/* Now copy the PTs */
					for (size_t k = 0; k < 512; ++k) {
						if (pd_in[k].bits.present) {
							union PML * pt_in = mmu_map_from_physical((uintptr_t)pd_in[k].bits.page << PAGE_SHIFT);
							spin_lock(frame_alloc_lock);
							uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
							mmu_frame_set(newPage);
							spin_unlock(frame_alloc_lock);
							union PML * pt_out = mmu_map_from_physical(newPage);
							memset(pt_out, 0, 512 * sizeof(union PML));
							pd_out[k].raw = (newPage) | USER_PML_ACCESS;

							/* Now, finally, copy pages */
							for (size_t l = 0; l < 512; ++l) {
								uintptr_t address = ((i << (9 * 3 + 12)) | (j << (9*2 + 12)) | (k << (9 + 12)) | (l << PAGE_SHIFT));
								if (address >= USER_DEVICE_MAP && address <= USER_SHM_HIGH) continue;
								if (pt_in[l].bits.present) {
									if (pt_in[l].bits.user) {
										copy_page_maybe(pt_in, pt_out, l, address);
									} else {
										/* If it's not a user page, just copy directly */
										pt_out[l].raw = pt_in[l].raw;
									}
								} /* Else, mmap'd files? */
							}
						}
					}
				}
			}
		}
	}

	return pml4_out;
}

/**
 * @brief Allocate one physical page.
 *
 * @returns a frame index, not an address
 */
uintptr_t mmu_allocate_a_frame(void) {
	spin_lock(frame_alloc_lock);
	uintptr_t index = mmu_first_frame();
	mmu_frame_set(index << PAGE_SHIFT);
	spin_unlock(frame_alloc_lock);
	return index;
}

/**
 * @brief Allocate a number of contiguous physical pages.
 *
 * @returns a frame index, not an address
 */
uintptr_t mmu_allocate_n_frames(int n) {
	spin_lock(frame_alloc_lock);
	uintptr_t index = mmu_first_n_frames(n);
	for (int i = 0; i < n; ++i) {
		mmu_frame_set((index+i) << PAGE_SHIFT);
	}
	spin_unlock(frame_alloc_lock);
	return index;
}

/**
 * @brief Scans a directory to calculate how many user pages are in use.
 *
 * Calculates how many pages a userspace application has mapped, between
 * its general memory space and stack. Excludes shared mappings, such
 * as SHM or mapped devices.
 *
 * TODO: This can probably be reduced to check a smaller range, but as we
 *       currently stick the user stack at the top of the low half of the
 *       address space we just scan everything and exclude shared memory...
 *
 * @param from Top-level page directory to scan.
 */
size_t mmu_count_user(union PML * from) {
	size_t out = 0;

	for (size_t i = 0; i < 256; ++i) {
		if (from[i].bits.present) {
			out++;
			union PML * pdp_in = mmu_map_from_physical((uintptr_t)from[i].bits.page << PAGE_SHIFT);
			for (size_t j = 0; j < 512; ++j) {
				if (pdp_in[j].bits.present) {
					out++;
					union PML * pd_in = mmu_map_from_physical((uintptr_t)pdp_in[j].bits.page << PAGE_SHIFT);
					for (size_t k = 0; k < 512; ++k) {
						if (pd_in[k].bits.present) {
							out++;
							union PML * pt_in = mmu_map_from_physical((uintptr_t)pd_in[k].bits.page << PAGE_SHIFT);
							for (size_t l = 0; l < 512; ++l) {
								/* Calculate final address to skip SHM */
								uintptr_t address = ((i << (9 * 3 + 12)) | (j << (9*2 + 12)) | (k << (9 + 12)) | (l << PAGE_SHIFT));
								if (address >= USER_DEVICE_MAP && address <= USER_SHM_HIGH) continue;
								if (pt_in[l].bits.present) {
									if (pt_in[l].bits.user) {
										out++;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return out;
}

/**
 * @brief Scans a directory to calculate how many shared memory pages are in use.
 *
 * At the moment, we only ever map shared pages to a specific region, so we just figure
 * out how many present pages are in that region and that's the answer.
 *
 * @param from Top-level page directory to scan.
 */
size_t mmu_count_shm(union PML * from) {
	size_t out = 0;

	for (size_t i = 0; i < 256; ++i) {
		if (from[i].bits.present) {
			union PML * pdp_in = mmu_map_from_physical((uintptr_t)from[i].bits.page << PAGE_SHIFT);
			for (size_t j = 0; j < 512; ++j) {
				if (pdp_in[j].bits.present) {
					union PML * pd_in = mmu_map_from_physical((uintptr_t)pdp_in[j].bits.page << PAGE_SHIFT);
					for (size_t k = 0; k < 512; ++k) {
						if (pd_in[k].bits.present) {
							union PML * pt_in = mmu_map_from_physical((uintptr_t)pd_in[k].bits.page << PAGE_SHIFT);
							for (size_t l = 0; l < 512; ++l) {
								/* Calculate final address to skip SHM */
								uintptr_t address = ((i << (9 * 3 + 12)) | (j << (9*2 + 12)) | (k << (9 + 12)) | (l << PAGE_SHIFT));
								if (address < USER_DEVICE_MAP || address > USER_SHM_HIGH) continue;
								if (pt_in[l].bits.present) {
									if (pt_in[l].bits.user) {
										out++;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return out;
}

/**
 * @brief Return the total amount of usable memory.
 *
 * @returns the total amount of usable memory in KiB.
 */
size_t mmu_total_memory(void) {
	return total_memory;
}

/**
 * @brief Return the amount of used memory.
 *
 * Calculates the number of pages currently marked as allocated.
 * Multiplies it by 4 because pages are 4KiB.
 *
 * @returns the amount of memory in use in KiB.
 */
size_t mmu_used_memory(void) {
	size_t ret = 0;
	size_t i, j;
	for (i = 0; i < INDEX_FROM_BIT(nframes); ++i) {
		for (j = 0; j < 32; ++j) {
			uint32_t testFrame = (uint32_t)0x1 << j;
			if (frames[i] & testFrame) {
				ret++;
			}
		}
	}
	return ret * 4 - unavailable_memory;
}

/**
 * @brief Relinquish pages owned by a top-level directory.
 *
 * Frees the underlying pages for a page directory within the lower (user) region.
 * Does not free kernel pages, as those are generally shared in the lower region.
 *
 * @param from Virtual pointer to top-level directory.
 */
void mmu_free(union PML * from) {
	if (!from) {
		printf("can't clear NULL directory\n");
		return;
	}

	spin_lock(frame_alloc_lock);
	for (size_t i = 0; i < 256; ++i) {
		if (from[i].bits.present) {
			union PML * pdp_in = mmu_map_from_physical((uintptr_t)from[i].bits.page << PAGE_SHIFT);
			for (size_t j = 0; j < 512; ++j) {
				if (pdp_in[j].bits.present) {
					union PML * pd_in = mmu_map_from_physical((uintptr_t)pdp_in[j].bits.page << PAGE_SHIFT);
					for (size_t k = 0; k < 512; ++k) {
						if (pd_in[k].bits.present) {
							union PML * pt_in = mmu_map_from_physical((uintptr_t)pd_in[k].bits.page << PAGE_SHIFT);
							for (size_t l = 0; l < 512; ++l) {
								uintptr_t address = ((i << (9 * 3 + 12)) | (j << (9*2 + 12)) | (k << (9 + 12)) | (l << PAGE_SHIFT));
								/* Do not free shared mappings; SHM subsystem does that for SHM, devices don't need it. */
								if (address >= USER_DEVICE_MAP && address <= USER_SHM_HIGH) continue;
								if (pt_in[l].bits.present) {
									/* Free only user pages */
									if (pt_in[l].bits.user) {
										free_page_maybe(pt_in,l,address);
									}
								}
							}
							mmu_frame_clear((uintptr_t)pd_in[k].bits.page << PAGE_SHIFT);
						}
					}
					mmu_frame_clear((uintptr_t)pdp_in[j].bits.page << PAGE_SHIFT);
				}
			}
			mmu_frame_clear((uintptr_t)from[i].bits.page << PAGE_SHIFT);
		}
	}

	mmu_frame_clear((((uintptr_t)from) & PHYS_MASK));
	spin_unlock(frame_alloc_lock);
}

union PML * mmu_get_kernel_directory(void) {
	return mmu_map_from_physical((uintptr_t)&init_page_region[0]);
}

/**
 * @brief Switch the active page directory for this core.
 *
 * Generally called during task creation and switching to change
 * the active page directory of a core. Updates @c this_core->current_pml.
 *
 * x86-64: Loads a given PML into CR3.
 *
 * @param new_pml Either the physical address or the shadow mapping virtual address
 *                of the new PML4 directory to switch into, general obtained from
 *                a process struct; if NULL is passed, the initial kernel directory
 *                will be used and no userspace mappings will be present.
 */
void mmu_set_directory(union PML * new_pml) {
	if (!new_pml) new_pml = mmu_map_from_physical((uintptr_t)&init_page_region[0]);
	this_core->current_pml = new_pml;

	asm volatile (
		"movq %0, %%cr3"
		: : "r"((uintptr_t)new_pml & PHYS_MASK));
}

/**
 * @brief Mark a virtual address's mappings as invalid in the TLB.
 *
 * Generally should be called when a mapping is relinquished, as this is what
 * the TLB caches, but is also called in a bunch of places where we're just mapping
 * new pages...
 *
 * @param addr Virtual address in the current address space to invalidate.
 */
void mmu_invalidate(uintptr_t addr) {
	asm volatile (
		"invlpg (%0)"
		: : "r"(addr));
	arch_tlb_shootdown(addr);
}

int mmu_get_page_deep(uintptr_t virtAddr, union PML ** pml4_out, union PML ** pdp_out, union PML ** pd_out, union PML ** pt_out) {
	/* This is all the same as x86, thankfully? */
	uintptr_t realBits = virtAddr & CANONICAL_MASK;
	uintptr_t pageAddr = realBits >> PAGE_SHIFT;
	unsigned int pml4_entry = (pageAddr >> 27) & ENTRY_MASK;
	unsigned int pdp_entry  = (pageAddr >> 18) & ENTRY_MASK;
	unsigned int pd_entry   = (pageAddr >> 9)  & ENTRY_MASK;
	unsigned int pt_entry   = (pageAddr) & ENTRY_MASK;

	/* Zero all the outputs */
	*pdp_out  = NULL;
	*pd_out   = NULL;
	*pt_out   = NULL;

	spin_lock(frame_alloc_lock);
	union PML * root = this_core->current_pml;
	*pml4_out = (union PML *)&root[pml4_entry];
	if (!root[pml4_entry].bits.present) goto _noentry;
	union PML * pdp = mmu_map_from_physical((uintptr_t)root[pml4_entry].bits.page << PAGE_SHIFT);
	*pdp_out = (union PML *)&pdp[pdp_entry];
	if (!pdp[pdp_entry].bits.present) goto _noentry;
	union PML * pd = mmu_map_from_physical((uintptr_t)pdp[pdp_entry].bits.page << PAGE_SHIFT);
	*pd_out = (union PML *)&pd[pd_entry];
	if (!pd[pd_entry].bits.present) goto _noentry;
	union PML * pt = mmu_map_from_physical((uintptr_t)pd[pd_entry].bits.page << PAGE_SHIFT);
	*pt_out = (union PML *)&pt[pt_entry];

	spin_unlock(frame_alloc_lock);
	return 0;

_noentry:
	spin_unlock(frame_alloc_lock);
	return 1;
}

static int maybe_release_directory(union PML * parent, union PML * child) {
	/* child points to one entry, to get the base, we can page align it */
	union PML * table = (union PML *)((uintptr_t)child & PAGE_SIZE_MASK);

	/* Is everything in the table free? */
	for (int i = 0; i < 512; ++i) {
		if (table[i].bits.present) return 0;
	}

	uintptr_t old_page = (parent->bits.page << PAGE_SHIFT);

	/* Then we can mark 'parent' as freed, clear the whole thing. */
	parent->raw = 0;
	mmu_frame_clear(old_page);

	return 1;
}

void mmu_unmap_user(uintptr_t addr, size_t size) {
	for (uintptr_t a = addr; a < addr + size; a += PAGE_SIZE) {
		union PML * pml4, * pdp, * pd, * pt;

		if (a >= USER_DEVICE_MAP && a <= USER_SHM_HIGH) continue;
		if (mmu_get_page_deep(a, &pml4, &pdp, &pd, &pt)) continue;

		spin_lock(frame_alloc_lock);

		if (pt && pt->bits.present && pt->bits.user) {
			if (pt->bits.writable) {
				assert(mem_refcounts[pt->bits.page] == 0);
				mmu_frame_clear((uintptr_t)pt->bits.page << PAGE_SHIFT);
			} else if (refcount_dec(pt->bits.page) == 0) {
				mmu_frame_clear((uintptr_t)pt->bits.page << PAGE_SHIFT);
			}
			pt->bits.present = 0;
			pt->bits.writable = 0;

			if (maybe_release_directory(pd, pt)) {
				if (maybe_release_directory(pdp, pd)) {
					maybe_release_directory(pml4, pdp);
				}
			}

			mmu_invalidate(a);
		}

		spin_unlock(frame_alloc_lock);
	}
}


static char * heapStart = NULL;
extern char end[];

/**
 * @brief Prepare virtual page mappings for use by the kernel.
 *
 * Called during early boot to switch from the loader/bootstrap mappings
 * to ones suitable for general use. Sets up the bitmap allocator, high
 * identity mapping, kernel heap, and various mid-level structures to
 * ensure that future kernelspace mappings apply to all kernel threads.
 *
 * @param memsize The maximum accessible physical address.
 * @param firstFreePage The address of the first frame the kernel may use for new allocations.
 */
void mmu_init(size_t memsize, uintptr_t firstFreePage) {
	this_core->current_pml = (union PML *)&init_page_region[0];

	/**
	 * Enable WP bit, which will cause kernel writes to
	 * non-writable pages to trigger page faults. We use
	 * this to perform COW mappings for user processes if
	 * they passed an unmapped region to a system call, though
	 * this should be handled by @see mmu_validate_user_pointer
	 * before we get to that point...
	 */
	asm volatile (
		"movq %%cr0, %%rax\n"
		"orq  $0x10000, %%rax\n"
		"movq %%rax, %%cr0\n"
		: : : "rax");

	/* Map the high base PDP */
	init_page_region[0][511].raw = (uintptr_t)&high_base_pml | KERNEL_PML_ACCESS;
	init_page_region[0][510].raw = (uintptr_t)&heap_base_pml | KERNEL_PML_ACCESS;

	/* Identity map from -128GB in the boot PML using 2MiB pages */
	for (size_t i = 0; i < 64; ++i) {
		high_base_pml[i].raw = (uintptr_t)&twom_high_pds[i] | KERNEL_PML_ACCESS;
		for (uintptr_t j = 0; j < 512; ++j) {
			twom_high_pds[i][j].raw = ((i << 30) + (j << 21)) | LARGE_PAGE_BIT | KERNEL_PML_ACCESS;
		}
	}

	/* Map low base PDP */
	low_base_pmls[0][0].raw = (uintptr_t)&low_base_pmls[1] | USER_PML_ACCESS;

	/* How much memory do we need to map low for our *kernel* to fit? */
	uintptr_t endPtr = ((uintptr_t)&end + PAGE_LOW_MASK) & PAGE_SIZE_MASK;

	/* How many pages does that need? */
	size_t lowPages = endPtr >> PAGE_SHIFT;

	/* And how many 512-page blocks does that fit in? */
	size_t pdCount = (lowPages + ENTRY_MASK) >> 9;

	for (size_t j = 0; j < pdCount; ++j) {
		low_base_pmls[1][j].raw = (uintptr_t)&low_base_pmls[2+j] | KERNEL_PML_ACCESS;
		for (int i = 0; i < 512; ++i) {
			low_base_pmls[2+j][i].raw = (uintptr_t)(LARGE_PAGE_SIZE * j + PAGE_SIZE * i) | KERNEL_PML_ACCESS;
		}
	}

	/* Unmap null */
	low_base_pmls[2][0].raw = 0;

	/* Now map our new low base */
	init_page_region[0][0].raw = (uintptr_t)&low_base_pmls[0] | USER_PML_ACCESS;

	/* Set up the page allocator bitmap... */
	nframes = (memsize >> 12);
	size_t bytesOfFrames = INDEX_FROM_BIT(nframes * 8);
	bytesOfFrames = (bytesOfFrames + PAGE_LOW_MASK) & PAGE_SIZE_MASK;
	firstFreePage = (firstFreePage + PAGE_LOW_MASK) & PAGE_SIZE_MASK;
	size_t pagesOfFrames = bytesOfFrames >> 12;

	/* Set up heap map for that... */
	heap_base_pml[0].raw = (uintptr_t)&heap_base_pd | KERNEL_PML_ACCESS;
	heap_base_pd[0].raw  = (uintptr_t)&heap_base_pt[0] | KERNEL_PML_ACCESS;
	heap_base_pd[1].raw  = (uintptr_t)&heap_base_pt[512] | KERNEL_PML_ACCESS;
	heap_base_pd[2].raw  = (uintptr_t)&heap_base_pt[1024] | KERNEL_PML_ACCESS;

	if (pagesOfFrames > 512*3) {
		printf("Warning: Too much available memory for current setup. Need %zu pages to represent allocation bitmap.\n", pagesOfFrames);
	}

	for (size_t i = 0; i < pagesOfFrames; i++) {
		heap_base_pt[i].raw = (firstFreePage + (i << 12)) | KERNEL_PML_ACCESS;
	}

	asm volatile ("" : : : "memory");
	this_core->current_pml = mmu_map_from_physical((uintptr_t)this_core->current_pml);
	asm volatile ("" : : : "memory");

	/* We are now in the new stuff. */
	frames = (void*)((uintptr_t)KERNEL_HEAP_START);
	memset((void*)frames, 0xFF, bytesOfFrames);

	extern void mboot_unmark_valid_memory(void);
	mboot_unmark_valid_memory();

	/* Don't trust anything but our own bitmap... */
	size_t unavail = 0, avail = 0;
	for (size_t i = 0; i < INDEX_FROM_BIT(nframes); ++i) {
		for (size_t j = 0; j < 32; ++j) {
			uint32_t testFrame = (uint32_t)0x1 << j;
			if (frames[i] & testFrame) {
				unavail++;
			} else {
				avail++;
			}
		}
	}

	total_memory = avail * 4;
	unavailable_memory = unavail * 4;

	/* Now mark everything up to (firstFreePage + bytesOfFrames) as in use */
	for (uintptr_t i = 0; i < firstFreePage + bytesOfFrames; i += PAGE_SIZE) {
		mmu_frame_set(i);
	}

	heapStart = (char*)KERNEL_HEAP_START + bytesOfFrames;

	/* Then, uh, make a bunch of space for page counts? */
	size_t size_of_refcounts = (nframes & PAGE_LOW_MASK) ? (nframes + PAGE_SIZE - (nframes & PAGE_LOW_MASK)) : nframes;
	mem_refcounts = sbrk(size_of_refcounts);
	memset(mem_refcounts, 0, size_of_refcounts);
}

/**
 * @brief Allocate space in the kernel virtual heap.
 *
 * Called by the kernel heap allocator to obtain space for new heap allocations.
 *
 * @warning Not to be confused with sys_sbrk
 *
 * @param bytes Bytes to allocate. Must be a multiple of PAGE_SIZE.
 * @returns The previous address of the break point, after which @p bytes may now be used.
 */
void * sbrk(size_t bytes) {
	if (!heapStart) {
		arch_fatal_prepare();
		printf("sbrk: Called before heap was ready.\n");
		arch_dump_traceback();
		arch_fatal();
	}

	if (!bytes) {
		/* Skip lock acquisition if we just wanted to know where the break was. */
		return heapStart;
	}

	if (bytes & PAGE_LOW_MASK) {
		arch_fatal_prepare();
		printf("sbrk: Size must be multiple of 4096, was %#zx\n", bytes);
		arch_dump_traceback();
		arch_fatal();
	}

	if (bytes > 0x1F00000) {
		arch_fatal_prepare();
		printf("sbrk: Size must be within a reasonable bound, was %#zx\n", bytes);
		arch_dump_traceback();
		arch_fatal();
	}

	spin_lock(kheap_lock);
	void * out = heapStart;

	for (uintptr_t p = (uintptr_t)out; p < (uintptr_t)out + bytes; p += PAGE_SIZE) {
		union PML * page = mmu_get_page(p, MMU_GET_MAKE);
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE | MMU_FLAG_KERNEL);
	}

	//memset(out, 0xAA, bytes);

	heapStart += bytes;
	spin_unlock(kheap_lock);
	return out;
}

static uintptr_t mmio_base_address = MMIO_BASE_START;

/**
 * @brief Obtain a writethrough region mapped to the given physical address.
 *
 * For use by device drivers to obtain mappings suitable for MMIO accesses. Note that the
 * virtual address space for these mappings can not be reclaimed, so drivers should keep
 * them around or use the other MMU facilities to repurpose them.
 *
 * @param physical_address Physical memory offset of the destination MMIO space.
 * @param size Size of the requested space, which must be a multiple of PAGE_SIZE.
 * @returns a virtual address suitable for MMIO accesses.
 */
void * mmu_map_mmio_region(uintptr_t physical_address, size_t size) {
	if (size & PAGE_LOW_MASK) {
		arch_fatal_prepare();
		printf("mmu_map_mmio_region: MMIO region size must be multiple of 4096 bytes, was %#zx.\n", size);
		arch_dump_traceback();
		arch_fatal();
	}

	spin_lock(mmio_space_lock);
	void * out = (void*)mmio_base_address;
	for (size_t i = 0; i < size; i += PAGE_SIZE) {
		union PML * p = mmu_get_page(mmio_base_address + i, MMU_GET_MAKE);
		mmu_frame_map_address(p, MMU_FLAG_KERNEL | MMU_FLAG_WRITABLE | MMU_FLAG_NOCACHE | MMU_FLAG_WRITETHROUGH, physical_address + i);
	}
	mmio_base_address += size;
	spin_unlock(mmio_space_lock);

	return out;
}

static uintptr_t module_base_address = MODULE_BASE_START;

/**
 * @brief Obtain space to load a module in the -2GiB region.
 *
 * This should really start immediately after the kernel, but we don't
 * yet load the kernel in the -2GiB region... it might also be worthwhile
 * to implement some ASLR here, especially given that we're loading
 * relocatable ELF object files and can stick them anywhere.
 *
 * @param size How much space to allocate, will be rounded up to page size.
 * @returns Start of the allocated address space.
 */
void * mmu_map_module(size_t size) {
	if (size & PAGE_LOW_MASK) {
		size += (PAGE_LOW_MASK + 1) - (size & PAGE_LOW_MASK);
	}

	spin_lock(module_space_lock);
	void * out = (void*)module_base_address;
	for (size_t i = 0; i < size; i += PAGE_SIZE) {
		union PML * p = mmu_get_page(module_base_address + i, MMU_GET_MAKE);
		mmu_frame_allocate(p, MMU_FLAG_KERNEL | MMU_FLAG_WRITABLE);
	}
	module_base_address += size;
	spin_unlock(module_space_lock);

	return out;
}

/**
 * @brief Free pages allocated for kernel modules.
 *
 * This rather blindly unmaps pages.
 *
 * @param start_address Start of mapping to unmap.
 * @param size Size of mapping to unmap.
 */
void mmu_unmap_module(uintptr_t start_address, size_t size) {
	if ((size & PAGE_LOW_MASK) || (start_address & PAGE_LOW_MASK)) {
		arch_fatal_prepare();
		printf("mmu_unmap_module start and size must be multiple of page size %#zx:%#zx.\n", start_address, size);
		arch_dump_traceback();
		arch_fatal();
	}

	spin_lock(module_space_lock);
	uintptr_t end_address = start_address + size;

	/* Unmap all pages we just allocated */
	for (uintptr_t i = start_address; i < end_address; i += 0x1000) {
		union PML * p = mmu_get_page(i, 0);
		mmu_frame_clear(p->bits.page << 12);
	}

	/* Reset module base address if it was at the end, to avoid wasting address space */
	if (end_address == module_base_address) {
		module_base_address = start_address;
	}
	spin_unlock(module_space_lock);
}

/**
 * @brief Swap a COW page for a writable copy.
 *
 * Examines @p address to determine if it is a pending
 * COW page that has been marked read-only. If it is,
 * it will be exchanged for a writable page. If it is
 * the last read-only reference to a page, it will be
 * marked writable without introducing a new backing page.
 *
 * @param address Virtual address that triggered the fault.
 * @returns 0 if this was a valid and completed COW operation, 1 otherwise.
 */
int mmu_copy_on_write(uintptr_t address) {
	union PML * page = mmu_get_page(address,0);

	/* Was this address pending a cow? */
	if (!page->bits.cow_pending) {
		/* No, go back and trigger and a SIGSEGV */
		return 1;
	}

	spin_lock(frame_alloc_lock);

	/* Is this the last reference to this page? */
	uint8_t refs = refcount_dec(page->bits.page);
	if (refs == 0) {
		/* Then we can just mark it writable. */
		page->bits.writable = 1;
		page->bits.cow_pending = 0;
		asm ("" ::: "memory");
		mmu_invalidate(address);
		spin_unlock(frame_alloc_lock);
		return 0;
	}

	/* Allocate a new writable page */
	uintptr_t faulting_frame = page->bits.page;
	uintptr_t fresh_frame = mmu_first_frame();
	mmu_frame_set(fresh_frame << PAGE_SHIFT);

	/* Copy the read-only page into the new writable page */
	char * page_in  = mmu_map_from_physical(faulting_frame << PAGE_SHIFT);
	char * page_out = mmu_map_from_physical(fresh_frame << PAGE_SHIFT);
	memcpy(page_out, page_in, 4096);

	/* And swap out the page table entry. */
	page->bits.page = fresh_frame;
	page->bits.writable = 1;
	page->bits.cow_pending = 0;
	spin_unlock(frame_alloc_lock);

	asm ("" ::: "memory");

	mmu_invalidate(address);
	return 0;
}

/**
 * @brief Check if the current user process can access address space.
 *
 * Thoroughly examines page table entries to determine if a user process
 * can access the memory at @p addr through @p size bytes.
 *
 * @p flags can be set to @c MMU_PTR_NULL if @c NULL address should trigger
 * a failure, @c MMU_PTR_WRITE if the process must have write access.
 *
 * @param addr Address to start checking from.
 * @param size Size after @p addr to check.
 * @param flags Control what constitutes a failure.
 * @returns 0 on failure, 1 if process has access.
 */
int mmu_validate_user_pointer(const void * addr, size_t size, int flags) {
	if (addr == NULL && !(flags & MMU_PTR_NULL)) return 0;
	if (size >     0x800000000000) return 0;

	uintptr_t base = (uintptr_t)addr;
	uintptr_t end  = size ? (base + (size - 1)) : base;

	/* Get start page, end page */
	uintptr_t page_base = base >> 12;
	uintptr_t page_end  =  end >> 12;

	for (uintptr_t page = page_base; page <= page_end; ++page) {
		if ((page & 0xffff800000000) != 0 && (page & 0xffff800000000) != 0xffff800000000) return 0;
		union PML * page_entry = mmu_get_page_other(this_core->current_process->thread.page_directory->directory, page << 12);
		if (!page_entry) return 0;
		if (!page_entry->bits.present) return 0;
		if (!page_entry->bits.user) return 0;
		if (!page_entry->bits.writable && (flags & MMU_PTR_WRITE)) {
			if (mmu_copy_on_write((uintptr_t)(page << 12))) return 0;
		}
	}

	return 1;
}


