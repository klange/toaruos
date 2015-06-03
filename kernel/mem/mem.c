/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 * Copyright (C) 2012 Markus Schober
 *
 * Kernel Memory Manager
 */

#include <mem.h>
#include <system.h>
#include <process.h>
#include <logging.h>
#include <signal.h>
#include <hashmap.h>
#include <module.h>

#define KERNEL_HEAP_INIT 0x00800000
#define KERNEL_HEAP_END  0x20000000

extern void *end;
uintptr_t placement_pointer = (uintptr_t)&end;
uintptr_t heap_end = (uintptr_t)NULL;
uintptr_t kernel_heap_alloc_point = KERNEL_HEAP_INIT;

//static volatile uint8_t frame_alloc_lock = 0;
static spin_lock_t frame_alloc_lock = { 0 };
uint32_t first_n_frames(int n);

void
kmalloc_startat(
		uintptr_t address
		) {
	placement_pointer = address;
}

/*
 * kmalloc() is the kernel's dumb placement allocator
 */
uintptr_t
kmalloc_real(
		size_t size,
		int align,
		uintptr_t * phys
		) {
	if (heap_end) {
		void * address;
		if (align) {
			address = valloc(size);
		} else {
			address = malloc(size);
		}
		if (phys) {
			if (align && size >= 0x3000) {
				debug_print(NOTICE, "Requested large aligned alloc of size 0x%x", size);
				for (uintptr_t i = (uintptr_t)address; i < (uintptr_t)address + size; i += 0x1000) {
					clear_frame(map_to_physical(i));
				}
				/* XXX This is going to get touchy... */
				spin_lock(frame_alloc_lock);
				uint32_t index = first_n_frames((size + 0xFFF) / 0x1000);
				if (index == 0xFFFFFFFF) {
					spin_unlock(frame_alloc_lock);
					return 0;
				}
				for (unsigned int i = 0; i < (size + 0xFFF) / 0x1000; ++i) {
					set_frame((index + i) * 0x1000);
					page_t * page = get_page((uintptr_t)address + (i * 0x1000),0,kernel_directory);
					page->frame = index + i;
				}
				spin_unlock(frame_alloc_lock);
			}
			*phys = map_to_physical((uintptr_t)address);
		}
		return (uintptr_t)address;
	}

	if (align && (placement_pointer & 0xFFFFF000)) {
		placement_pointer &= 0xFFFFF000;
		placement_pointer += 0x1000;
	}
	if (phys) {
		*phys = placement_pointer;
	}
	uintptr_t address = placement_pointer;
	placement_pointer += size;
	return (uintptr_t)address;
}
/*
 * Normal
 */
uintptr_t
kmalloc(
		size_t size
		) {
	return kmalloc_real(size, 0, NULL);
}
/*
 * Aligned
 */
uintptr_t
kvmalloc(
		size_t size
		) {
	return kmalloc_real(size, 1, NULL);
}
/*
 * With a physical address
 */
uintptr_t
kmalloc_p(
		size_t size,
		uintptr_t *phys
		) {
	return kmalloc_real(size, 0, phys);
}
/*
 * Aligned, with a physical address
 */
uintptr_t
kvmalloc_p(
		size_t size,
		uintptr_t *phys
		) {
	return kmalloc_real(size, 1, phys);
}

/*
 * Frame Allocation
 */

uint32_t *frames;
uint32_t nframes;

#define INDEX_FROM_BIT(b) (b / 0x20)
#define OFFSET_FROM_BIT(b) (b % 0x20)

void
set_frame(
		uintptr_t frame_addr
		) {
	if (frame_addr < nframes * 4 * 0x400) {
		uint32_t frame  = frame_addr / 0x1000;
		uint32_t index  = INDEX_FROM_BIT(frame);
		uint32_t offset = OFFSET_FROM_BIT(frame);
		frames[index] |= (0x1 << offset);
	}
}

void
clear_frame(
		uintptr_t frame_addr
		) {
	uint32_t frame  = frame_addr / 0x1000;
	uint32_t index  = INDEX_FROM_BIT(frame);
	uint32_t offset = OFFSET_FROM_BIT(frame);
	frames[index] &= ~(0x1 << offset);
}

uint32_t test_frame(uintptr_t frame_addr) {
	uint32_t frame  = frame_addr / 0x1000;
	uint32_t index  = INDEX_FROM_BIT(frame);
	uint32_t offset = OFFSET_FROM_BIT(frame);
	return (frames[index] & (0x1 << offset));
}

uint32_t first_n_frames(int n) {
	for (uint32_t i = 0; i < nframes * 0x1000; i += 0x1000) {
		int bad = 0;
		for (int j = 0; j < n; ++j) {
			if (test_frame(i + 0x1000 * j)) {
				bad = j+1;
			}
		}
		if (!bad) {
			return i / 0x1000;
		}
	}
	return 0xFFFFFFFF;
}

uint32_t first_frame(void) {
	uint32_t i, j;

	for (i = 0; i < INDEX_FROM_BIT(nframes); ++i) {
		if (frames[i] != 0xFFFFFFFF) {
			for (j = 0; j < 32; ++j) {
				uint32_t testFrame = 0x1 << j;
				if (!(frames[i] & testFrame)) {
					return i * 0x20 + j;
				}
			}
		}
	}

	debug_print(CRITICAL, "System claims to be out of usable memory, which means we probably overwrote the page frames.\033[0m");

	if (debug_video_crash) {
		char * msgs[] = {"Out of memory.", NULL};
		debug_video_crash(msgs);
	}

#if 0
	signal_t * sig = malloc(sizeof(signal_t));
	sig->handler = current_process->signals.functions[SIGSEGV];
	sig->signum  = SIGSEGV;
	handle_signal((process_t *)current_process, sig);
#endif

	STOP;

	return -1;
}

void
alloc_frame(
		page_t *page,
		int is_kernel,
		int is_writeable
		) {
	if (page->frame != 0) {
		page->present = 1;
		page->rw      = (is_writeable == 1) ? 1 : 0;
		page->user    = (is_kernel == 1)    ? 0 : 1;
		return;
	} else {
		spin_lock(frame_alloc_lock);
		uint32_t index = first_frame();
		assert(index != (uint32_t)-1 && "Out of frames.");
		set_frame(index * 0x1000);
		page->frame   = index;
		spin_unlock(frame_alloc_lock);
		page->present = 1;
		page->rw      = (is_writeable == 1) ? 1 : 0;
		page->user    = (is_kernel == 1)    ? 0 : 1;
	}
}

void
dma_frame(
		page_t *page,
		int is_kernel,
		int is_writeable,
		uintptr_t address
		) {
	/* Page this address directly */
	page->present = 1;
	page->rw      = (is_writeable) ? 1 : 0;
	page->user    = (is_kernel)    ? 0 : 1;
	page->frame   = address / 0x1000;
	set_frame(address);
}

void
free_frame(
		page_t *page
		) {
	uint32_t frame;
	if (!(frame = page->frame)) {
		assert(0);
		return;
	} else {
		clear_frame(frame * 0x1000);
		page->frame = 0x0;
	}
}

uintptr_t memory_use(void ) {
	uintptr_t ret = 0;
	uint32_t i, j;
	for (i = 0; i < INDEX_FROM_BIT(nframes); ++i) {
		for (j = 0; j < 32; ++j) {
			uint32_t testFrame = 0x1 << j;
			if (frames[i] & testFrame) {
				ret++;
			}
		}
	}
	return ret * 4;
}

uintptr_t memory_total(){
	return nframes * 4;
}

void paging_install(uint32_t memsize) {
	nframes = memsize  / 4;
	frames  = (uint32_t *)kmalloc(INDEX_FROM_BIT(nframes * 8));
	memset(frames, 0, INDEX_FROM_BIT(nframes * 8));

	uintptr_t phys;
	kernel_directory = (page_directory_t *)kvmalloc_p(sizeof(page_directory_t),&phys);
	memset(kernel_directory, 0, sizeof(page_directory_t));
}

void paging_mark_system(uint64_t addr) {
	set_frame(addr);
}

void paging_finalize(void) {
	debug_print(INFO, "Placement pointer is at 0x%x", placement_pointer);
#if 1
	get_page(0,1,kernel_directory)->present = 0;
	set_frame(0);
	for (uintptr_t i = 0x1000; i < 0x80000; i += 0x1000) {
#else
	for (uintptr_t i = 0x0; i < 0x80000; i += 0x1000) {
#endif
		dma_frame(get_page(i, 1, kernel_directory), 1, 0, i);
	}
	for (uintptr_t i = 0x80000; i < 0x100000; i += 0x1000) {
		dma_frame(get_page(i, 1, kernel_directory), 1, 0, i);
	}
	for (uintptr_t i = 0x100000; i < placement_pointer + 0x3000; i += 0x1000) {
		dma_frame(get_page(i, 1, kernel_directory), 1, 0, i);
	}
	debug_print(INFO, "Mapping VGA text-mode directly.");
	for (uintptr_t j = 0xb8000; j < 0xc0000; j += 0x1000) {
		dma_frame(get_page(j, 0, kernel_directory), 0, 1, j);
	}
	isrs_install_handler(14, page_fault);
	kernel_directory->physical_address = (uintptr_t)kernel_directory->physical_tables;

	uintptr_t tmp_heap_start = KERNEL_HEAP_INIT;

	if (tmp_heap_start <= placement_pointer + 0x3000) {
		debug_print(ERROR, "Foo: 0x%x, 0x%x", tmp_heap_start, placement_pointer + 0x3000);
		tmp_heap_start = placement_pointer + 0x100000;
		kernel_heap_alloc_point = tmp_heap_start;
	}

	/* Kernel Heap Space */
	for (uintptr_t i = placement_pointer + 0x3000; i < tmp_heap_start; i += 0x1000) {
		alloc_frame(get_page(i, 1, kernel_directory), 1, 0);
	}
	/* And preallocate the page entries for all the rest of the kernel heap as well */
	for (uintptr_t i = tmp_heap_start; i < KERNEL_HEAP_END; i += 0x1000) {
		get_page(i, 1, kernel_directory);
	}

	debug_print(NOTICE, "Setting directory.");
	current_directory = clone_directory(kernel_directory);
	switch_page_directory(kernel_directory);
}

uintptr_t map_to_physical(uintptr_t virtual) {
	uintptr_t remaining = virtual % 0x1000;
	uintptr_t frame = virtual / 0x1000;
	uintptr_t table = frame / 1024;
	uintptr_t subframe = frame % 1024;

	if (current_directory->tables[table]) {
		page_t * p = &current_directory->tables[table]->pages[subframe];
		return p->frame * 0x1000 + remaining;
	} else {
		return 0;
	}
}

void debug_print_directory(page_directory_t * arg) {
	page_directory_t * current_directory = arg;
	debug_print(INSANE, " ---- [k:0x%x u:0x%x]", kernel_directory, current_directory);
	for (uintptr_t i = 0; i < 1024; ++i) {
		if (!current_directory->tables[i] || (uintptr_t)current_directory->tables[i] == (uintptr_t)0xFFFFFFFF) {
			continue;
		}
		if (kernel_directory->tables[i] == current_directory->tables[i]) {
			debug_print(INSANE, "  0x%x - kern [0x%x/0x%x] 0x%x", current_directory->tables[i], &current_directory->tables[i], &kernel_directory->tables[i], i * 0x1000 * 1024);
			for (uint16_t j = 0; j < 1024; ++j) {
#if 1
				page_t *  p= &current_directory->tables[i]->pages[j];
				if (p->frame) {
					debug_print(INSANE, " k  0x%x 0x%x %s", (i * 1024 + j) * 0x1000, p->frame * 0x1000, p->present ? "[present]" : "");
				}
#endif
			}
		} else {
			debug_print(INSANE, "  0x%x - user [0x%x] 0x%x [0x%x]", current_directory->tables[i], &current_directory->tables[i], i * 0x1000 * 1024, kernel_directory->tables[i]);
			for (uint16_t j = 0; j < 1024; ++j) {
#if 1
				page_t *  p= &current_directory->tables[i]->pages[j];
				if (p->frame) {
					debug_print(INSANE, "    0x%x 0x%x %s", (i * 1024 + j) * 0x1000, p->frame * 0x1000, p->present ? "[present]" : "");
				}
#endif
			}
		}
	}
	debug_print(INFO, " ---- [done]");
}

void
switch_page_directory(
		page_directory_t * dir
		) {
	current_directory = dir;
	asm volatile (
			"mov %0, %%cr3\n"
			"mov %%cr0, %%eax\n"
			"orl $0x80000000, %%eax\n"
			"mov %%eax, %%cr0\n"
			:: "r"(dir->physical_address)
			: "%eax");
}

void invalidate_page_tables(void) {
	asm volatile (
			"movl %%cr3, %%eax\n"
			"movl %%eax, %%cr3\n"
			::: "%eax");
}

void invalidate_tables_at(uintptr_t addr) {
	asm volatile (
			"movl %0,%%eax\n"
			"invlpg (%%eax)\n"
			:: "r"(addr) : "%eax");
}

page_t *
get_page(
		uintptr_t address,
		int make,
		page_directory_t * dir
		) {
	address /= 0x1000;
	uint32_t table_index = address / 1024;
	if (dir->tables[table_index]) {
		return &dir->tables[table_index]->pages[address % 1024];
	} else if(make) {
		uint32_t temp;
		dir->tables[table_index] = (page_table_t *)kvmalloc_p(sizeof(page_table_t), (uintptr_t *)(&temp));
		memset(dir->tables[table_index], 0, sizeof(page_table_t));
		dir->physical_tables[table_index] = temp | 0x7; /* Present, R/w, User */
		return &dir->tables[table_index]->pages[address % 1024];
	} else {
		return 0;
	}
}

void
page_fault(
		struct regs *r)  {
	uint32_t faulting_address;
	asm volatile("mov %%cr2, %0" : "=r"(faulting_address));

	if (r->eip == SIGNAL_RETURN) {
		return_from_signal_handler();
	} else if (r->eip == THREAD_RETURN) {
		debug_print(INFO, "Returned from thread.");
		kexit(0);
	}

#if 1
	int present  = !(r->err_code & 0x1) ? 1 : 0;
	int rw       = r->err_code & 0x2    ? 1 : 0;
	int user     = r->err_code & 0x4    ? 1 : 0;
	int reserved = r->err_code & 0x8    ? 1 : 0;
	int id       = r->err_code & 0x10   ? 1 : 0;

	debug_print(ERROR, "\033[1;37;41mSegmentation fault. (p:%d,rw:%d,user:%d,res:%d,id:%d) at 0x%x eip: 0x%x pid=%d,%d [%s]\033[0m",
			present, rw, user, reserved, id, faulting_address, r->eip, current_process->id, current_process->group, current_process->name);

	if (r->eip < heap_end) {
		/* find closest symbol */
		char * closest  = NULL;
		size_t distance = 0xFFFFFFFF;
		uintptr_t  addr = 0;

		if (modules_get_symbols()) {
			list_t * hash_keys = hashmap_keys(modules_get_symbols());
			foreach(_key, hash_keys) {
				char * key = (char *)_key->value;
				uintptr_t a = (uintptr_t)hashmap_get(modules_get_symbols(), key);

				if (!a) continue;

				size_t d;
				if (a <= r->eip) {
					d = r->eip - a;
				} else {
					d = a - r->eip;
				}
				if (d < distance) {
					closest = key;
					distance = d;
					addr = a;
				}
			}
			free(hash_keys);

			debug_print(ERROR, "\033[1;31mClosest symbol to faulting address:\033[0m %s [0x%x]", closest, addr);

			hash_keys = hashmap_keys(modules_get_list());
			foreach(_key, hash_keys) {
				char * key = (char *)_key->value;
				module_data_t * m = (module_data_t *)hashmap_get(modules_get_list(), key);

				if ((r->eip >= (uintptr_t)m->bin_data) && (r->eip < m->end)) {
					debug_print(ERROR, "\033[1;31mIn module:\033[0m %s (starts at 0x%x)", m->mod_info->name, m->bin_data);
					break;
				}
			}
			free(hash_keys);

			debug_print(ERROR, "User EIP: 0x%x", current_process->syscall_registers->eip);
		}

	} else {
		debug_print(ERROR, "\033[1;31m(In userspace)\033[0m");
	}

#endif

	signal_t * sig = malloc(sizeof(signal_t));
	sig->handler = current_process->signals.functions[SIGSEGV];
	sig->signum  = SIGSEGV;
	handle_signal((process_t *)current_process, sig);

}

/*
 * Heap
 * Stop using kalloc and friends after installing the heap
 * otherwise shit will break. I've conveniently broken
 * kalloc when installing the heap, just for those of you
 * who feel the need to screw up.
 */


void heap_install(void ) {
	heap_end = (placement_pointer + 0x1000) & ~0xFFF;
}

void * sbrk(uintptr_t increment) {
	assert((increment % 0x1000 == 0) && "Kernel requested to expand heap by a non-page-multiple value");
	assert((heap_end % 0x1000 == 0)  && "Kernel heap is not page-aligned!");
	assert((heap_end + increment <= KERNEL_HEAP_END - 1) && "The kernel has attempted to allocate beyond the end of its heap.");
	uintptr_t address = heap_end;

	if (heap_end + increment > kernel_heap_alloc_point) {
		debug_print(INFO, "Hit the end of available kernel heap, going to allocate more (at 0x%x, want to be at 0x%x)", heap_end, heap_end + increment);
		for (uintptr_t i = heap_end; i < heap_end + increment; i += 0x1000) {
			debug_print(INFO, "Allocating frame at 0x%x...", i);
			alloc_frame(get_page(i, 0, kernel_directory), 1, 0);
		}
		invalidate_page_tables();
		debug_print(INFO, "Done.");
	}

	heap_end += increment;
	memset((void *)address, 0x0, increment);
	return (void *)address;
}

