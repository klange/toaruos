/**
 * @file  kernel/arch/aarch64/mmu.c
 * @brief Nearly identical to the x86-64 implementation.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2022 K. Lange
 */
#include <stdint.h>
#include <kernel/assert.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/misc.h>
#include <kernel/mmu.h>

static volatile uint32_t *frames;
static size_t nframes;
static size_t total_memory = 0;
static size_t unavailable_memory = 0;
static uint64_t ram_starts_at = 0;

uintptr_t aarch64_kernel_phys_base = 0;

/* TODO Used for CoW later. */
//static uint8_t * mem_refcounts = NULL;

#define PAGE_SHIFT     12
#define PAGE_SIZE      0x1000UL
#define PAGE_SIZE_MASK 0xFFFFffffFFFFf000UL
#define PAGE_LOW_MASK  0x0000000000000FFFUL

#define LARGE_PAGE_SIZE 0x200000UL

#define PHYS_MASK 0x7fffffffffUL
#define CANONICAL_MASK 0xFFFFffffFFFFUL

#define INDEX_FROM_BIT(b)  ((b) >> 5)
#define OFFSET_FROM_BIT(b) ((b) & 0x1F)

#define _pagemap __attribute__((aligned(PAGE_SIZE))) = {0}
union PML init_page_region[512] _pagemap;
union PML high_base_pml[512] _pagemap;
union PML heap_base_pml[512] _pagemap;
union PML heap_base_pd[512] _pagemap;
union PML heap_base_pt[512*3] _pagemap;
union PML kbase_pmls[65][512] _pagemap;

#define PTE_VALID      (1UL << 0)
#define PTE_TABLE      (1UL << 1)

/* Table attributes */
#define PTE_NSTABLE    (1UL << 63)
#define PTE_APTABLE    (3UL << 61) /* two bits */
#define  PTE_APTABLE_A (1UL << 62)
#define  PTE_APTABLE_B (1UL << 61)
#define PTE_UXNTABLE   (1UL << 60)
#define PTE_PXNTABLE   (1UL << 59)

/* Block attributes */
#define PTE_UXN        (1UL << 54)
#define PTE_PXN        (1UL << 53)
#define PTE_CONTIGUOUS (1UL << 52)
#define PTE_NG         (1UL << 11)
#define PTE_AF         (1UL << 10)
#define PTE_SH         (3UL << 8)  /* two bits */
#define  PTE_SH_A      (1UL << 9)
#define  PTE_SH_B      (1UL << 8)
#define PTE_AP         (3UL << 6)  /* two bits */
#define  PTE_AP_A      (1UL << 7)
#define  PTE_AP_B      (1UL << 6)
#define PTE_NS         (1UL << 5)
#define PTE_ATTRINDX   (7UL << 2) /* three bits */
#define  PTE_ATTR_A    (1UL << 4)
#define  PTE_ATTR_B    (1UL << 3)
#define  PTE_ATTR_C    (1UL << 2)

void mmu_frame_set(uintptr_t frame_addr) {
	if (frame_addr < ram_starts_at) return;
	frame_addr -= ram_starts_at;
	if (frame_addr < nframes * PAGE_SIZE) {
		uint64_t frame  = frame_addr >> 12;
		uint64_t index  = INDEX_FROM_BIT(frame);
		uint32_t offset = OFFSET_FROM_BIT(frame);
		__sync_or_and_fetch(&frames[index], ((uint32_t)1 << offset));
		asm ("isb" ::: "memory");
	}
}

static uintptr_t lowest_available = 0;

void mmu_frame_clear(uintptr_t frame_addr) {
	if (frame_addr < ram_starts_at) return;
	frame_addr -= ram_starts_at;
	if (frame_addr < nframes * PAGE_SIZE) {
		uint64_t frame  = frame_addr >> PAGE_SHIFT;
		uint64_t index  = INDEX_FROM_BIT(frame);
		uint32_t offset = OFFSET_FROM_BIT(frame);
		__sync_and_and_fetch(&frames[index], ~((uint32_t)1 << offset));
		asm ("isb" ::: "memory");
		if (frame < lowest_available) lowest_available = frame;
	}
}

int mmu_frame_test(uintptr_t frame_addr) {
	if (frame_addr < ram_starts_at) return 1;
	frame_addr -= ram_starts_at;
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

uintptr_t mmu_first_n_frames(int n) {
	for (uint64_t i = 0; i < nframes * PAGE_SIZE; i += PAGE_SIZE) {
		int bad = 0;
		for (int j = 0; j < n; ++j) {
			if (mmu_frame_test(i + ram_starts_at + PAGE_SIZE * j)) {
				bad = j + 1;
			}
		}
		if (!bad) {
			return (i + ram_starts_at) / PAGE_SIZE;
		}
	}

	arch_fatal_prepare();
	dprintf("Failed to allocate %d contiguous frames.\n", n);
	arch_dump_traceback();
	arch_fatal();
	return (uintptr_t)-1;
}

uintptr_t mmu_first_frame(void) {
	uintptr_t i, j;
	for (i = INDEX_FROM_BIT(lowest_available); i < INDEX_FROM_BIT(nframes); ++i) {
		if (frames[i] != (uint32_t)-1) {
			for (j = 0; j < (sizeof(uint32_t)*8); ++j) {
				uint32_t testFrame = (uint32_t)1 << j;
				if (!(frames[i] & testFrame)) {
					uintptr_t out = (i << 5) + j;
					lowest_available = out + 1;
					return out + (ram_starts_at >> 12);
				}
			}
		}
	}

	if (lowest_available != 0) {
		lowest_available = 0;
		return mmu_first_frame();
	}

	arch_fatal_prepare();
	dprintf("Out of memory.\n");
	arch_dump_traceback();
	arch_fatal();
	return (uintptr_t)-1;
}

void mmu_frame_allocate(union PML * page, unsigned int flags) {
	/* If page is not set... */
	if (page->bits.page == 0) {
		spin_lock(frame_alloc_lock);
		uintptr_t index = mmu_first_frame();
		mmu_frame_set(index << PAGE_SHIFT);
		page->bits.page     = index;
		spin_unlock(frame_alloc_lock);
	}

	page->bits.table_page = 1;
	page->bits.present    = 1;

	page->bits.ap = (!(flags & MMU_FLAG_WRITABLE) ? 2 : 0) | (!(flags & MMU_FLAG_KERNEL) ? 1 : 0);
	page->bits.af = 1;
	page->bits.sh = 2;
	page->bits.attrindx = ((flags & MMU_FLAG_NOCACHE) | (flags & MMU_FLAG_WRITETHROUGH)) ? 0 : 1;

	if (!(flags & MMU_FLAG_KERNEL)) {
		page->bits.attrindx = 1;

		if ((flags & MMU_FLAG_WC) == MMU_FLAG_WC) {
			page->bits.attrindx = 2;
		}
	}

	asm volatile ("dsb ishst\ntlbi vmalle1is\ndsb ish\nisb" ::: "memory");

	#if 0
	page->bits.writable = (flags & MMU_FLAG_WRITABLE) ? 1 : 0;
	page->bits.user     = (flags & MMU_FLAG_KERNEL)   ? 0 : 1;
	page->bits.nocache  = (flags & MMU_FLAG_NOCACHE)  ? 1 : 0;
	page->bits.writethrough  = (flags & MMU_FLAG_WRITETHROUGH)  ? 1 : 0;
	page->bits.size     = (flags & MMU_FLAG_SPEC) ? 1 : 0;
	page->bits.nx       = (flags & MMU_FLAG_NOEXECUTE) ? 1 : 0;
	#endif

}

void mmu_frame_map_address(union PML * page, unsigned int flags, uintptr_t physAddr) {
	/* frame set physAddr, set page in entry, call frame_allocate to set attribute bits */
	mmu_frame_set(physAddr);
	page->bits.page = physAddr >> PAGE_SHIFT;
	mmu_frame_allocate(page, flags);
}

void * mmu_map_from_physical(uintptr_t frameaddress) {
	return (void*)(frameaddress | HIGH_MAP_REGION);
}

#define PDP_MASK 0x3fffffffUL
#define  PD_MASK 0x1fffffUL
#define  PT_MASK PAGE_LOW_MASK
#define ENTRY_MASK 0x1FF
union PML * mmu_get_page_other(union PML * root, uintptr_t virtAddr) {
	//printf("mmu_get_page_other(%#zx, %#zx);\n", (uintptr_t)root, virtAddr);
	/* Walk it */
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

	if (!pdp[pdp_entry].bits.table_page) {
		return NULL;
	}

	union PML * pd = mmu_map_from_physical((uintptr_t)pdp[pdp_entry].bits.page << PAGE_SHIFT);

	if (!pd[pd_entry].bits.present) {
		return NULL;
	}

	if (!pd[pd_entry].bits.table_page) {
		return NULL;
	}

	union PML * pt = mmu_map_from_physical((uintptr_t)pd[pd_entry].bits.page << PAGE_SHIFT);
	return (union PML *)&pt[pt_entry];
}

uintptr_t mmu_map_to_physical(union PML * root, uintptr_t virtAddr) {
	if (!root) {
		if (virtAddr >= MODULE_BASE_START) {
			return (virtAddr - MODULE_BASE_START) + aarch64_kernel_phys_base;
		} else if (virtAddr >= HIGH_MAP_REGION) {
			return (virtAddr - HIGH_MAP_REGION);
		}
		return (uintptr_t)virtAddr;
	}

	uintptr_t realBits = virtAddr & CANONICAL_MASK;
	uintptr_t pageAddr = realBits >> PAGE_SHIFT;
	unsigned int pml4_entry = (pageAddr >> 27) & ENTRY_MASK;
	unsigned int pdp_entry  = (pageAddr >> 18) & ENTRY_MASK;
	unsigned int pd_entry   = (pageAddr >> 9)  & ENTRY_MASK;
	unsigned int pt_entry   = (pageAddr) & ENTRY_MASK;

	if (!root[pml4_entry].bits.present) return (uintptr_t)-1;

	union PML * pdp = mmu_map_from_physical((uintptr_t)root[pml4_entry].bits.page << PAGE_SHIFT);

	if (!pdp[pdp_entry].bits.present) return (uintptr_t)-2;
	if (!pdp[pdp_entry].bits.table_page) return ((uintptr_t)pdp[pdp_entry].bits.page << PAGE_SHIFT) | (virtAddr & PDP_MASK);

	union PML * pd = mmu_map_from_physical((uintptr_t)pdp[pdp_entry].bits.page << PAGE_SHIFT);

	if (!pd[pd_entry].bits.present) return (uintptr_t)-3;
	if (!pd[pd_entry].bits.table_page) return ((uintptr_t)pd[pd_entry].bits.page << PAGE_SHIFT) | (virtAddr & PD_MASK);

	union PML * pt = mmu_map_from_physical((uintptr_t)pd[pd_entry].bits.page << PAGE_SHIFT);

	if (!pt[pt_entry].bits.present) return (uintptr_t)-4;
	return ((uintptr_t)pt[pt_entry].bits.page << PAGE_SHIFT) | (virtAddr & PT_MASK);
}

union PML * mmu_get_page(uintptr_t virtAddr, int flags) {
	/* This is all the same as x86, thankfully? */
	uintptr_t realBits = virtAddr & CANONICAL_MASK;
	uintptr_t pageAddr = realBits >> PAGE_SHIFT;
	unsigned int pml4_entry = (pageAddr >> 27) & ENTRY_MASK;
	unsigned int pdp_entry  = (pageAddr >> 18) & ENTRY_MASK;
	unsigned int pd_entry   = (pageAddr >> 9)  & ENTRY_MASK;
	unsigned int pt_entry   = (pageAddr) & ENTRY_MASK;

	union PML * root = this_core->current_pml;

	/* Get the PML4 entry for this address */
	spin_lock(frame_alloc_lock);
	if (!root[pml4_entry].bits.present) {
		if (!(flags & MMU_GET_MAKE)) goto _noentry;
		uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
		mmu_frame_set(newPage);
		/* zero it */
		memset(mmu_map_from_physical(newPage), 0, PAGE_SIZE);
		root[pml4_entry].raw = (newPage) | PTE_VALID | PTE_TABLE | PTE_AF;
	}
	spin_unlock(frame_alloc_lock);

	union PML * pdp = mmu_map_from_physical((uintptr_t)root[pml4_entry].bits.page << PAGE_SHIFT);

	spin_lock(frame_alloc_lock);
	if (!pdp[pdp_entry].bits.present) {
		if (!(flags & MMU_GET_MAKE)) goto _noentry;
		uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
		mmu_frame_set(newPage);
		/* zero it */
		memset(mmu_map_from_physical(newPage), 0, PAGE_SIZE);
		pdp[pdp_entry].raw = (newPage) | PTE_VALID | PTE_TABLE | PTE_AF;
	}
	spin_unlock(frame_alloc_lock);

	if (!pdp[pdp_entry].bits.table_page) {
		printf("Warning: Tried to get page for a 1GiB block! %d\n", pdp_entry);
		return NULL;
	}

	union PML * pd = mmu_map_from_physical((uintptr_t)pdp[pdp_entry].bits.page << PAGE_SHIFT);

	spin_lock(frame_alloc_lock);
	if (!pd[pd_entry].bits.present) {
		if (!(flags & MMU_GET_MAKE)) goto _noentry;
		uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
		mmu_frame_set(newPage);
		/* zero it */
		memset(mmu_map_from_physical(newPage), 0, PAGE_SIZE);
		pd[pd_entry].raw = (newPage) | PTE_VALID | PTE_TABLE | PTE_AF;
	}
	spin_unlock(frame_alloc_lock);

	if (!pd[pd_entry].bits.table_page) {
		printf("Warning: Tried to get page for a 2MiB block!\n");
		return NULL;
	}

	union PML * pt = mmu_map_from_physical((uintptr_t)pd[pd_entry].bits.page << PAGE_SHIFT);
	return (union PML *)&pt[pt_entry];

_noentry:
	spin_unlock(frame_alloc_lock);
	printf("no entry for requested page\n");
	return NULL;
}

static int copy_page_maybe(union PML * pt_in, union PML * pt_out, size_t l, uintptr_t address) {
	spin_lock(frame_alloc_lock);

	/* TODO cow bits */

	char * page_in = mmu_map_from_physical((uintptr_t)pt_in[l].bits.page << PAGE_SHIFT);
	uintptr_t newPage = mmu_first_frame() << PAGE_SHIFT;
	mmu_frame_set(newPage);
	char * page_out = mmu_map_from_physical(newPage);
	memcpy(page_out,page_in,PAGE_SIZE);
	asm volatile ("dmb sy\nisb" ::: "memory");

	for (uintptr_t x = (uintptr_t)page_out; x < (uintptr_t)page_out + PAGE_SIZE; x += 64) {
		asm volatile ("dc cvau, %0" :: "r"(x));
	}
	for (uintptr_t x = (uintptr_t)page_out; x < (uintptr_t)page_out + PAGE_SIZE; x += 64) {
		asm volatile ("ic ivau, %0" :: "r"(x));
	}

	pt_out[l].raw = 0;
	pt_out[l].bits.table_page = 1;
	pt_out[l].bits.present = 1;
	pt_out[l].bits.ap = pt_in[l].bits.ap;
	pt_out[l].bits.af = pt_in[l].bits.af;
	pt_out[l].bits.sh = pt_in[l].bits.sh;
	pt_out[l].bits.attrindx = pt_in[l].bits.attrindx;
	pt_out[l].bits.page = newPage >> PAGE_SHIFT;
	asm volatile ("" ::: "memory");

	spin_unlock(frame_alloc_lock);
	return 0;
}

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
			pml4_out[i].raw = (newPage) | PTE_VALID | PTE_TABLE | PTE_AF;

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
					pdp_out[j].raw = (newPage) | PTE_VALID | PTE_TABLE | PTE_AF;

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
							pd_out[k].raw = (newPage) | PTE_VALID | PTE_TABLE | PTE_AF;

							/* Now, finally, copy pages */
							for (size_t l = 0; l < 512; ++l) {
								uintptr_t address = ((i << (9 * 3 + 12)) | (j << (9*2 + 12)) | (k << (9 + 12)) | (l << PAGE_SHIFT));
								if (address >= USER_DEVICE_MAP && address <= USER_SHM_HIGH) continue;
								if (pt_in[l].bits.present) {
									if (1) { //pt_in[l].bits.user) {
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

uintptr_t mmu_allocate_a_frame(void) {
	spin_lock(frame_alloc_lock);
	uintptr_t index = mmu_first_frame();
	mmu_frame_set(index << PAGE_SHIFT);
	spin_unlock(frame_alloc_lock);
	return index;
}

uintptr_t mmu_allocate_n_frames(int n) {
	spin_lock(frame_alloc_lock);
	uintptr_t index = mmu_first_n_frames(n);
	for (int i = 0; i < n; ++i) {
		mmu_frame_set((index+i) << PAGE_SHIFT);
	}
	spin_unlock(frame_alloc_lock);
	return index;
}

size_t mmu_count_user(union PML * from) {
	/* We walk 'from' and count user pages */
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
									if (pt_in[l].bits.ap & 1) {
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

size_t mmu_count_shm(union PML * from) {
	/* We walk 'from' and count shm region stuff */
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
								if (address < USER_DEVICE_MAP && address >= USER_SHM_HIGH) continue;
								if (pt_in[l].bits.present) {
									if (pt_in[l].bits.ap & 1) {
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

size_t mmu_total_memory(void) {
	return total_memory;
}

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

void mmu_free(union PML * from) {
	/* walk and free pages */
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
									if (pt_in[l].bits.ap & 1) {
										mmu_frame_clear((uintptr_t)pt_in[l].bits.page << PAGE_SHIFT);
										pt_in[l].raw = 0;
										//free_page_maybe(pt_in,l,address);
									}
								}
							}
							mmu_frame_clear((uintptr_t)pd_in[k].bits.page << PAGE_SHIFT);
							pd_in[k].raw = 0;
						}
					}
					mmu_frame_clear((uintptr_t)pdp_in[j].bits.page << PAGE_SHIFT);
					pdp_in[j].raw = 0;
				}
			}
			mmu_frame_clear((uintptr_t)from[i].bits.page << PAGE_SHIFT);
			from[i].raw = 0;
		}
	}

	uintptr_t physAddr = (((uintptr_t)from) & PHYS_MASK);
	mmu_frame_clear(physAddr);
	asm volatile ("dsb ishst\ntlbi vmalle1is\ndsb ish\nisb" ::: "memory");
	spin_unlock(frame_alloc_lock);
}

union PML * mmu_get_kernel_directory(void) {
	return mmu_map_from_physical((uintptr_t)&init_page_region - MODULE_BASE_START + aarch64_kernel_phys_base);
}

void mmu_set_directory(union PML * new_pml) {
	/* Set the EL0 and EL1 directy things?
	 *   There are two of these... */
	if (!new_pml) new_pml = mmu_get_kernel_directory();
	this_core->current_pml = new_pml;
	uintptr_t pml_phys = mmu_map_to_physical(new_pml, (uintptr_t)new_pml);

	asm volatile (
		"msr TTBR0_EL1,%0\n"
		"msr TTBR1_EL1,%0\n"
		"isb sy\n"
		"dsb ishst\n"
		"tlbi vmalle1is\n"
		"dsb ish\n"
		"isb\n" :: "r"(pml_phys) : "memory");
}

void mmu_invalidate(uintptr_t addr) {
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

		/* Free this page if it was present */
		if (pt && pt->bits.present) {
			if (pt->bits.ap & 1) {
				mmu_frame_clear((uintptr_t)pt->bits.page << PAGE_SHIFT);
				pt->bits.present = 0;
				pt->bits.ap = 0;
			}

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

	spin_lock(kheap_lock);
	void * out = heapStart;

	for (uintptr_t p = (uintptr_t)out; p < (uintptr_t)out + bytes; p += PAGE_SIZE) {
		union PML * page = mmu_get_page(p, MMU_GET_MAKE);
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE | MMU_FLAG_KERNEL);
	}

	heapStart += bytes;
	spin_unlock(kheap_lock);
	return out;
}

static uintptr_t mmio_base_address = MMIO_BASE_START;
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

void mmu_unmap_module(uintptr_t start_address, size_t size) {
}

int mmu_copy_on_write(uintptr_t address) {
	
	return 1;
}

int mmu_validate_user_pointer(void * addr, size_t size, int flags) {
	//printf("mmu_validate_user_pointer(%#zx, %lu, %u);\n", (uintptr_t)addr, size, flags);
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
		if (!page_entry) {
			return 0;
		}
		if (!page_entry->bits.present) {
			return 0;
		}
		if (!(page_entry->bits.ap & 1)) {
			return 0;
		}
		if ((page_entry->bits.ap & 2) && (flags & MMU_PTR_WRITE)) {
			return 0;
			//if (mmu_copy_on_write((uintptr_t)(page << 12))) return 0;
		}
	}

	return 1;
}

static uintptr_t k2p(void * x) {
	return ((uintptr_t)x - MODULE_BASE_START) + aarch64_kernel_phys_base;
}

void mmu_init(uintptr_t memaddr, size_t memsize, uintptr_t firstFreePage, uintptr_t endOfRamDisk) {
	this_core->current_pml = (union PML*)mmu_get_kernel_directory();

	/* Convert from bytes to kibibytes */
	total_memory = memsize / 1024;

	/* We don't currently support gaps in this setup. */
	unavailable_memory = 0;

	/* MAIR setup? */
	uint64_t mair = (0x000000000044ff00);
	asm volatile ("msr MAIR_EL1,%0" :: "r"(mair));
	asm volatile ("mrs %0,MAIR_EL1" : "=r"(mair));
	dprintf("mmu: MAIR_EL1=0x%016lx\n", mair);

	asm volatile ("" ::: "memory");

	/* Replicate the mapping we already have */
	init_page_region[511].raw = k2p(&high_base_pml) | PTE_VALID | PTE_TABLE | PTE_AF;
	init_page_region[510].raw = k2p(&heap_base_pml) | PTE_VALID | PTE_TABLE | PTE_AF;

	/* "Identity" map at -512GiB */
	for (size_t i = 0; i < 64; ++i) {
		high_base_pml[i].raw = (i << 30) | PTE_VALID | PTE_AF | (1 << 2);
	}


	/* Set up some space to map us */

	/* How many 2MiB spans do we need to cover to endOfRamDisk? */
	size_t twoms  = (endOfRamDisk + (LARGE_PAGE_SIZE - 1)) / LARGE_PAGE_SIZE;

	/* init_page_region[511] -> high_base_pml[510] -> kbase_pmls[0] -> kbase_pmls[1+n] */
	high_base_pml[510].raw = k2p(&kbase_pmls[0]) | PTE_VALID | PTE_TABLE | PTE_AF;
	for (size_t j = 0; j < twoms; ++j) {
		kbase_pmls[0][j].raw = k2p(&kbase_pmls[1+j]) | PTE_VALID | PTE_TABLE | PTE_AF;
		for (int i = 0; i < 512; ++i) {
			kbase_pmls[1+j][i].raw = (uintptr_t)(aarch64_kernel_phys_base + LARGE_PAGE_SIZE * j + PAGE_SIZE * i) |
				PTE_VALID | PTE_AF | PTE_SH_A | PTE_TABLE | (1 << 2);
		}
	}

	/* We should be ready to switch to our page directory? */
	asm volatile ("msr TTBR0_EL1,%0" : : "r"(k2p(&init_page_region)));
	asm volatile ("msr TTBR1_EL1,%0" : : "r"(k2p(&init_page_region)));
	asm volatile ("dsb ishst\ntlbi vmalle1is\ndsb ish\nisb" ::: "memory");

	/* Let's map some heap. */
	heap_base_pml[0].raw = k2p(&heap_base_pd)       | PTE_VALID | PTE_TABLE | PTE_AF;
	heap_base_pd[0].raw  = k2p(&heap_base_pt[0])    | PTE_VALID | PTE_TABLE | PTE_AF;
	heap_base_pd[1].raw  = k2p(&heap_base_pt[512])  | PTE_VALID | PTE_TABLE | PTE_AF;
	heap_base_pd[2].raw  = k2p(&heap_base_pt[1024]) | PTE_VALID | PTE_TABLE | PTE_AF;

	/* Physical frame allocator. We're gonna do this the same as the one we have x86-64, because
	 * I can't be bothered to think of anything better right now... */
	ram_starts_at = memaddr;
	nframes = (memsize) >> 12;
	size_t bytesOfFrames = INDEX_FROM_BIT(nframes * 8);
	bytesOfFrames = (bytesOfFrames + PAGE_LOW_MASK) & PAGE_SIZE_MASK;

	/* TODO we should figure out where the DTB ends on virt, as that's where we can
	 *      start doing this... */
	size_t pagesOfFrames = bytesOfFrames >> 12;

	/* Map pages for it... */
	for (size_t i = 0; i < pagesOfFrames; ++i) {
		heap_base_pt[i].raw = (firstFreePage + (i << 12)) | PTE_VALID | PTE_AF | PTE_SH_A | PTE_TABLE | (1 << 2);
	}

	asm volatile ("dsb ishst\ntlbi vmalle1is\ndsb ish\nisb" ::: "memory");

	/* Just assume everything is in use. */
	frames = (void*)((uintptr_t)KERNEL_HEAP_START);
	memset((void*)frames, 0x00, bytesOfFrames);

	/* Set frames as in use... */
	for (uintptr_t i = memaddr; i < firstFreePage + bytesOfFrames; i+= PAGE_SIZE) {
		mmu_frame_set(i);
	}

	/* Set kernel space as in use */
	for (uintptr_t i = 0; i < twoms * LARGE_PAGE_SIZE; i += PAGE_SIZE) {
		mmu_frame_set(aarch64_kernel_phys_base + i);
	}

	heapStart = (char*)KERNEL_HEAP_START + bytesOfFrames;

	lowest_available = (firstFreePage + bytesOfFrames) - memaddr;
	module_base_address = endOfRamDisk + MODULE_BASE_START;
	if (module_base_address & PAGE_LOW_MASK) {
		module_base_address = (module_base_address & PAGE_SIZE_MASK) + PAGE_SIZE;
	}
}
