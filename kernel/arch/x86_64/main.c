/**
 * @file  kernel/arch/x86_64/main.c
 * @brief Intel/AMD x86-64 (IA64/amd64) architecture-specific startup.
 *
 * Parses multiboot data, sets up GDT/IDT/TSS, initializes PML4 paging,
 * and sets up PC device drivers (PS/2, port I/O, serial).
 */
#include <kernel/types.h>
#include <kernel/multiboot.h>
#include <kernel/symboltable.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/pci.h>
#include <kernel/hashmap.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>
#include <kernel/video.h>
#include <kernel/generic.h>
#include <kernel/gzip.h>
#include <kernel/ramdisk.h>
#include <kernel/args.h>
#include <kernel/ksym.h>
#include <kernel/misc.h>
#include <kernel/version.h>
#include <kernel/elf.h>

#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/cmos.h>
#include <kernel/arch/x86_64/pml.h>

#include <errno.h>

extern void arch_clock_initialize(void);

extern char end[];
extern unsigned long tsc_mhz;

extern void gdt_install(void);
extern void idt_install(void);
extern void pic_initialize(void);
extern void pit_initialize(void);
extern void smp_initialize(void);
extern void portio_initialize(void);
extern void ps2hid_install(void);
extern void serial_initialize(void);
extern void fbterm_initialize(void);
extern void pci_remap(void);
extern void mmu_init(size_t memsize, uintptr_t firstFreePage);

struct multiboot * mboot_struct = NULL;
int mboot_is_2 = 0;

static int _serial_debug = 1;
#define EARLY_LOG_DEVICE 0x3F8
static size_t _early_log_write(size_t size, uint8_t * buffer) {
	if (!_serial_debug) return size;

	for (unsigned int i = 0; i < size; ++i) {
		outportb(EARLY_LOG_DEVICE, buffer[i]);
	}
	return size;
}

static void early_log_initialize(void) {
	outportb(EARLY_LOG_DEVICE + 3, 0x03); /* Disable divisor mode, set parity */
	printf_output = &_early_log_write;
}

static uintptr_t highest_valid_address = 0;
static uintptr_t highest_kernel_address = (uintptr_t)&end;

struct MB2_TagHeader {
	uint32_t type;
	uint32_t size;
};

void * mboot2_find_next(char * current, uint32_t type) {
	char * header = current;
	while ((uintptr_t)header & 7) header++;
	struct MB2_TagHeader * tag = (void*)header;
	while (1) {
		if (tag->type == 0) return NULL;
		if (tag->type == type) return tag;
		/* Next tag */
		header += tag->size;
		while ((uintptr_t)header & 7) header++;
		tag = (void*)header;
	}
}

void * mboot2_find_tag(void * fromStruct, uint32_t type) {
	char * header = (void*)fromStruct;
	header += 8;
	return mboot2_find_next(header, type);
}

struct MB2_MemoryMap {
	struct MB2_TagHeader head;
	uint32_t entry_size;
	uint32_t entry_version;
	char entries[];
};

struct MB2_MemoryMap_Entry {
	uint64_t base_addr;
	uint64_t length;
	uint32_t type;
	uint32_t reserved;
};

struct MB2_Framebuffer {
	struct MB2_TagHeader head;
	uint64_t addr;
	uint32_t pitch;
	uint32_t width;
	uint32_t height;
	uint8_t bpp;
	uint8_t fb_type;
};

struct MB2_Module {
	struct MB2_TagHeader head;
	uint32_t mod_start;
	uint32_t mod_end;
	uint8_t  cmdline[];
};

static void multiboot2_initialize(void * mboot) {
	mboot_is_2 = 1;
	dprintf("multiboot: Started with a Multiboot 2 loader\n");

	struct MB2_MemoryMap * mmap = mboot2_find_tag(mboot, 6);
	if (!mmap) {
		printf("fatal: unable to boot without memory map from loader\n");
		arch_fatal();
	}

	char * entry = mmap->entries;
	while ((uintptr_t)entry < (uintptr_t)mmap + mmap->head.size) {
		struct MB2_MemoryMap_Entry * this = (void*)entry;
		if (this->type == 1 && this->length && this->base_addr + this->length - 1> highest_valid_address) {
			highest_valid_address = this->base_addr + this->length - 1;
		}
		entry += mmap->entry_size;
	}

	struct MB2_Module * mod = mboot2_find_tag(mboot, 3);
	while (mod) {
		uintptr_t addr = (uintptr_t)mod->mod_end;
		if (addr > highest_kernel_address) highest_kernel_address = addr;
		mod = mboot2_find_next((char*)mod + mod->head.size, 3);
	}

	/* Round the max address up a page */
	highest_kernel_address = (highest_kernel_address + 0xFFF) & 0xFFFFffffFFFFf000;
}

static void multiboot_initialize(struct multiboot * mboot) {

	dprintf("multiboot: Started with a Multiboot 1 loader\n");

	if (!(mboot->flags & MULTIBOOT_FLAG_MMAP)) {
		printf("fatal: unable to boot without memory map from loader\n");
		arch_fatal();
	}

	mboot_memmap_t * mmap = (void *)(uintptr_t)mboot->mmap_addr;

	if ((uintptr_t)mmap + mboot->mmap_length > highest_kernel_address) {
		highest_kernel_address = (uintptr_t)mmap + mboot->mmap_length;
	}

	while ((uintptr_t)mmap < mboot->mmap_addr + mboot->mmap_length) {
		if (mmap->type == 1 && mmap->length && mmap->base_addr + mmap->length - 1> highest_valid_address) {
			highest_valid_address = mmap->base_addr + mmap->length - 1;
		}
		mmap = (mboot_memmap_t *) ((uintptr_t)mmap + mmap->size + sizeof(uint32_t));
	}

	if (mboot->flags & MULTIBOOT_FLAG_MODS) {
		mboot_mod_t * mods = (mboot_mod_t *)(uintptr_t)mboot->mods_addr;
		for (unsigned int i = 0; i < mboot->mods_count; ++i) {
			uintptr_t addr = (uintptr_t)mods[i].mod_end;
			if (addr > highest_kernel_address) {
				highest_kernel_address = addr;
			}
		}
	}

	/* Round the max address up a page */
	highest_kernel_address = (highest_kernel_address + 0xFFF) & 0xFFFFffffFFFFf000;
}

void mboot_unmark_valid_memory(void) {
	size_t frames_marked = 0;
	if (mboot_is_2) {
		struct MB2_MemoryMap * mmap = mboot2_find_tag(mboot_struct, 6);
		char * entry = mmap->entries;
		while ((uintptr_t)entry < (uintptr_t)mmap + mmap->head.size) {
			struct MB2_MemoryMap_Entry * this = (void*)entry;
			if (this->type == 1) {
				for (uintptr_t base = this->base_addr; base < this->base_addr + (this->length & 0xFFFFffffFFFFf000); base += 0x1000) {
					mmu_frame_clear(base);
					frames_marked++;
				}
			}
			entry += mmap->entry_size;
		}
	} else {
		mboot_memmap_t * mmap = mmu_map_from_physical((uintptr_t)mboot_struct->mmap_addr);
		while ((uintptr_t)mmap < (uintptr_t)mmu_map_from_physical(mboot_struct->mmap_addr + mboot_struct->mmap_length)) {
			if (mmap->type == 1) {
				for (uintptr_t base = mmap->base_addr; base < mmap->base_addr + (mmap->length & 0xFFFFffffFFFFf000); base += 0x1000) {
					mmu_frame_clear(base);
					frames_marked++;
				}
			}
			mmap = (mboot_memmap_t *) ((uintptr_t)mmap + mmap->size + sizeof(uint32_t));
		}
	}
}

static void symbols_install(uint64_t base) {
	ksym_install();
	kernel_symbol_t * k = (kernel_symbol_t *)&kernel_symbols_start;
	while ((uintptr_t)k < (uintptr_t)&kernel_symbols_end) {
		ksym_bind(k->name, (void*)(k->addr + base));
		k = (kernel_symbol_t *)((uintptr_t)k + sizeof *k + strlen(k->name) + 1);
	}
}

/**
 * @brief Initializes the page attribute table.
 * FIXME: This seems to be assuming the lower entries are
 *        already sane - we should probably initialize all
 *        of the entries ourselves.
 */
void pat_initialize(void) {
	asm volatile (
		"mov $0x277, %%ecx\n" /* IA32_MSR_PAT */
		"rdmsr\n"
		"or $0x1000000, %%edx\n" /* set bit 56 */
		"and $0xf9ffffff, %%edx\n" /* unset bits 57, 58 */
		"wrmsr\n"
		: : : "ecx", "edx", "eax"
	);
}

/**
 * @brief Turns on the floating-point unit.
 *
 * Enables a few bits so we can get SSE.
 *
 * We don't do any fancy lazy FPU reload as x86-64 assumes a wide
 * variety of FPU-provided registers are available so most userspace
 * code will be messing with the FPU anyway and we'd probably just
 * waste time with all the interrupts turning it off and on...
 */
void fpu_initialize(void) {
	asm volatile (
		"clts\n"
		"mov %%cr0, %%rax\n"
		"and $0xFFFD, %%ax\n"
		"or $0x10, %%ax\n"
		"mov %%rax, %%cr0\n"
		"fninit\n"
		"mov %%cr0, %%rax\n"
		"and $0xfffb, %%ax\n"
		"or  $0x0002, %%ax\n"
		"mov %%rax, %%cr0\n"
		"mov %%cr4, %%rax\n"
		"or $0x600, %%rax\n"
		"mov %%rax, %%cr4\n"
		"push $0x1F80\n"
		"ldmxcsr (%%rsp)\n"
		"addq $8, %%rsp\n"
	: : : "rax");
}

static void mount_ramdisk(uintptr_t addr, size_t len) {
	uint8_t * data = mmu_map_from_physical(addr);
	if (data[0] == 0x1F && data[1] == 0x8B) {
		/* Yes - decompress it first */
		dprintf("multiboot: Decompressing initial ramdisk...\n");
		uint32_t decompressedSize = *(uint32_t*)mmu_map_from_physical(addr + len - sizeof(uint32_t));
		size_t pageCount = (((size_t)decompressedSize + 0xFFF) & ~(0xFFF)) >> 12;
		uintptr_t physicalAddress = mmu_allocate_n_frames(pageCount) << 12;

		if (physicalAddress == (uintptr_t)-1) {
			dprintf("gzip: failed to allocate pages\n");
			return;
		}
		gzip_inputPtr = (void*)data;
		gzip_outputPtr = mmu_map_from_physical(physicalAddress);
		/* Do the deed */
		if (gzip_decompress()) {
			dprintf("gzip: failed to decompress payload\n");
			return;
		}
		ramdisk_mount(physicalAddress, decompressedSize);
		dprintf("multiboot: Decompressed %lu kB to %u kB.\n",
			(len) / 1024,
			(decompressedSize) / 1024);
		/* Free the pages from the original mod */
		for (size_t j = addr; j < addr + len; j += 0x1000) {
			mmu_frame_clear(j);
		}
	} else {
		/* No, or it doesn't look like one - mount it directly */
		dprintf("multiboot: Mounting uncompressed ramdisk.\n");
		ramdisk_mount(addr, len);
	}
}

/**
 * @brief Decompress compressed ramdisks and hand them to the ramdisk driver.
 *
 * Reads through the list of modules passed by a multiboot-compatible loader
 * and determines if they are gzip-compressed, decompresses if they are, and
 * finally hands them to the VFS driver. The VFS ramdisk driver takes control
 * of linear sets of physical pages, and handles mapping them somewhere to
 * provide reads in userspace, as well as freeing them if requested.
 */
void mount_multiboot_ramdisks(struct multiboot * mboot) {
	/* ramdisk_mount takes physical pages, it will map them itself. */
	if (mboot_is_2) {
		struct MB2_Module * mod = mboot2_find_tag(mboot_struct, 3);
		while (mod) {
			uintptr_t address = mod->mod_start;
			size_t    length  = mod->mod_end - mod->mod_start;
			mount_ramdisk(address, length);
			mod = mboot2_find_next((char*)mod + mod->head.size, 3);
		}
	} else {
		mboot_mod_t * mods = mmu_map_from_physical(mboot->mods_addr);
		for (unsigned int i = 0; i < mboot->mods_count; ++i) {
			uint64_t address = mods[i].mod_start;
			uint64_t length  = mods[i].mod_end - mods[i].mod_start;
			mount_ramdisk(address, length);
		}
	}
}

/**
 * x86-64: The kernel commandline is retrieved from the multiboot struct.
 */
const char * arch_get_cmdline(void) {
	if (mboot_is_2) {
		struct loader { uint32_t type; uint32_t size; char name[]; } * loader = mboot2_find_tag(mboot_struct, 1);
		if (loader) {
			return loader->name;
		}
		return "";
	} else {
		return mmu_map_from_physical(mboot_struct->cmdline);
	}
}

/**
 * x86-64: The bootloader name is retrieved from the multiboot struct.
 */
const char * arch_get_loader(void) {
	if (mboot_is_2) {
		struct loader { uint32_t type; uint32_t size; char name[]; } * loader = mboot2_find_tag(mboot_struct, 2);
		if (loader) {
			return loader->name;
		}
	} else if (mboot_struct->flags & MULTIBOOT_FLAG_LOADER) {
		return mmu_map_from_physical(mboot_struct->boot_loader_name);
	}
	return "(unknown)";
}

/**
 * x86-64: The GS register, which is set by a pair of MSRs, tracks per-CPU kernel state.
 */
void arch_set_core_base(uintptr_t base) {
	asm volatile ("wrmsr" : : "c"(0xc0000101), "d"((uint32_t)(base >> 32)), "a"((uint32_t)(base & 0xFFFFFFFF)));
	asm volatile ("wrmsr" : : "c"(0xc0000102), "d"((uint32_t)(base >> 32)), "a"((uint32_t)(base & 0xFFFFFFFF)));
	asm volatile ("swapgs");
}

void arch_framebuffer_initialize(void) {
	extern uint8_t * lfb_vid_memory;
	extern uint16_t lfb_resolution_x;
	extern uint16_t lfb_resolution_y;
	extern uint32_t lfb_resolution_s;
	extern uint16_t lfb_resolution_b;

	if (!mboot_is_2) {
		lfb_vid_memory = mmu_map_from_physical(mboot_struct->framebuffer_addr);
		lfb_resolution_x = mboot_struct->framebuffer_width;
		lfb_resolution_y = mboot_struct->framebuffer_height;
		lfb_resolution_s = mboot_struct->framebuffer_pitch;
		lfb_resolution_b = mboot_struct->framebuffer_bpp;
	} else {
		struct MB2_Framebuffer * fb = mboot2_find_tag(mboot_struct, 8);
		if (fb) {
			lfb_vid_memory = mmu_map_from_physical(fb->addr);
			lfb_resolution_x = fb->width;
			lfb_resolution_y = fb->height;
			lfb_resolution_s = fb->pitch;
			lfb_resolution_b = fb->bpp;
		}
	}
}

/**
 * @brief x86-64 multiboot C entrypoint.
 *
 * Called by the x86-64 longmode bootstrap.
 */
int kmain(struct multiboot * mboot, uint32_t mboot_mag, void* esp, uint64_t base) {
	extern Elf64_Rela _rela_start[], _rela_end[];
	for (Elf64_Rela * rela = _rela_start; rela < _rela_end; ++rela) {
		switch (ELF64_R_TYPE(rela->r_info)) {
			case R_X86_64_RELATIVE:
				*(uint64_t*)(rela->r_offset + base) = base + rela->r_addend;
				break;
		}
	}

	/* The debug log is over /dev/ttyS0, but skips the PTY interface; it's available
	 * as soon as we can call printf(), which is as soon as we get to long mode. */
	early_log_initialize();

	dprintf("%s %d.%d.%d-%s %s %s\n",
		__kernel_name,
		__kernel_version_major,
		__kernel_version_minor,
		__kernel_version_lower,
		__kernel_version_suffix,
		__kernel_version_codename,
		__kernel_arch);

	/* Initialize GS base */
	arch_set_core_base((uintptr_t)&processor_local_data[0]);

	/* Time the TSC and get the initial boot time from the RTC. */
	arch_clock_initialize();

	/* Parse multiboot data so we can get memory map, modules, command line, etc. */
	if (mboot_mag == 0x36d76289) {
		multiboot2_initialize(mboot);
	} else {
		multiboot_initialize(mboot);
	}

	/* multiboot memory is now mapped high, if you want it. */
	mboot_struct = mmu_map_from_physical((uintptr_t)mboot);

	/* memCount and maxAddress come from multiboot data */
	mmu_init(highest_valid_address, highest_kernel_address);

	/* With the MMU initialized, set up things required for the scheduler. */
	pat_initialize();
	symbols_install(base);
	gdt_install();
	idt_install();
	fpu_initialize();
	pic_initialize();

	/* Early generic stuff */
	generic_startup();

	/* Should we override the TSC timing? */
	if (args_present("tsc_mhz")) {
		tsc_mhz = atoi(args_value("tsc_mhz"));
	}

	if (!args_present("debug")) {
		_serial_debug = 0;
	}

	/* Scheduler is running and we have parsed the kcmdline, initialize video. */
	framebuffer_initialize();
	fbterm_initialize();

	/* Start up other cores and enable an appropriate preempt source. */
	smp_initialize();

	/* Decompress and mount all initial ramdisks. */
	mount_multiboot_ramdisks(mboot_struct);

	/* Install generic PC device drivers. */
	ps2hid_install();
	serial_initialize();
	portio_initialize();

	/* Yield to the generic main, which starts /bin/init */
	return generic_main();
}
