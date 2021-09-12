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

#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/cmos.h>
#include <kernel/arch/x86_64/pml.h>

#include <errno.h>

extern void arch_clock_initialize(void);

extern char end[];

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

struct multiboot * mboot_struct = NULL;

#define EARLY_LOG_DEVICE 0x3F8
static size_t _early_log_write(size_t size, uint8_t * buffer) {
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

static void multiboot_initialize(struct multiboot * mboot) {
	if (!(mboot->flags & MULTIBOOT_FLAG_MMAP)) {
		printf("fatal: unable to boot without memory map from loader\n");
		arch_fatal();
	}

	mboot_memmap_t * mmap = (void *)(uintptr_t)mboot->mmap_addr;
	while ((uintptr_t)mmap < mboot->mmap_addr + mboot->mmap_length) {
		if (mmap->type == 1 && mmap->length && mmap->base_addr + mmap->length - 1> highest_valid_address) {
			highest_valid_address = mmap->base_addr + mmap->length - 1;
		}
		mmap = (mboot_memmap_t *) ((uintptr_t)mmap + mmap->size + sizeof(uint32_t));
	}

	if (mboot->flags & MULTIBOOT_FLAG_MODS) {
		mboot_mod_t * mods = (mboot_mod_t *)(uintptr_t)mboot->mods_addr;
		for (unsigned int i = 0; i < mboot->mods_count; ++i) {
			uintptr_t addr = (uintptr_t)mods[i].mod_start + mods[i].mod_end;
			if (addr > highest_kernel_address) highest_kernel_address = addr;
		}
	}

	/* Round the max address up a page */
	highest_kernel_address = (highest_kernel_address + 0xFFF) & 0xFFFFffffFFFFf000;
}

void mboot_unmark_valid_memory(void) {
	/* multiboot memory is now mapped high, if you want it. */
	mboot_memmap_t * mmap = mmu_map_from_physical((uintptr_t)mboot_struct->mmap_addr);
	size_t frames_marked = 0;
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

static void symbols_install(void) {
	ksym_install();
	kernel_symbol_t * k = (kernel_symbol_t *)&kernel_symbols_start;
	while ((uintptr_t)k < (uintptr_t)&kernel_symbols_end) {
		ksym_bind(k->name, (void*)k->addr);
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
	mboot_mod_t * mods = mmu_map_from_physical(mboot->mods_addr);

	for (unsigned int i = 0; i < mboot->mods_count; ++i) {
		/* Is this a gzipped data source? */
		uint8_t * data = mmu_map_from_physical(mods[i].mod_start);
		if (data[0] == 0x1F && data[1] == 0x8B) {
			/* Yes - decompress it first */
			uint32_t decompressedSize = *(uint32_t*)mmu_map_from_physical(mods[i].mod_end - sizeof(uint32_t));
			size_t pageCount = (((size_t)decompressedSize + 0xFFF) & ~(0xFFF)) >> 12;
			uintptr_t physicalAddress = mmu_allocate_n_frames(pageCount) << 12;
			if (physicalAddress == (uintptr_t)-1) {
				printf("gzip: failed to allocate pages for decompressed payload, skipping\n");
				continue;
			}
			gzip_inputPtr = (void*)data;
			gzip_outputPtr = mmu_map_from_physical(physicalAddress);
			/* Do the deed */
			if (gzip_decompress()) {
				printf("gzip: failed to decompress payload, skipping\n");
				continue;
			}
			ramdisk_mount(physicalAddress, decompressedSize);
			/* Free the pages from the original mod */
			for (size_t j = mods[i].mod_start; j < mods[i].mod_end; j += 0x1000) {
				mmu_frame_clear(j);
			}
		} else {
			/* No, or it doesn't look like one - mount it directly */
			ramdisk_mount(mods[i].mod_start, mods[i].mod_end - mods[i].mod_start);
		}
	}
}

/**
 * x86-64: The kernel commandline is retrieved from the multiboot struct.
 */
const char * arch_get_cmdline(void) {
	return mmu_map_from_physical(mboot_struct->cmdline);
}

/**
 * x86-64: The bootloader name is retrieved from the multiboot struct.
 */
const char * arch_get_loader(void) {
	if (mboot_struct->flags & MULTIBOOT_FLAG_LOADER) {
		return mmu_map_from_physical(mboot_struct->boot_loader_name);
	} else {
		return "(unknown)";
	}
}

/**
 * x86-64: The GS register, which is set by a pair of MSRs, tracks per-CPU kernel state.
 */
void arch_set_core_base(uintptr_t base) {
	asm volatile ("wrmsr" : : "c"(0xc0000101), "d"((uint32_t)(base >> 32)), "a"((uint32_t)(base & 0xFFFFFFFF)));
	asm volatile ("wrmsr" : : "c"(0xc0000102), "d"((uint32_t)(base >> 32)), "a"((uint32_t)(base & 0xFFFFFFFF)));
	asm volatile ("swapgs");
}

/**
 * @brief x86-64 multiboot C entrypoint.
 *
 * Called by the x86-64 longmode bootstrap.
 */
int kmain(struct multiboot * mboot, uint32_t mboot_mag, void* esp) {
	/* The debug log is over /dev/ttyS0, but skips the PTY interface; it's available
	 * as soon as we can call printf(), which is as soon as we get to long mode. */
	early_log_initialize();

	/* Initialize GS base */
	arch_set_core_base((uintptr_t)&processor_local_data[0]);

	/* Time the TSC and get the initial boot time from the RTC. */
	arch_clock_initialize();

	/* Parse multiboot data so we can get memory map, modules, command line, etc. */
	multiboot_initialize(mboot);

	/* multiboot memory is now mapped high, if you want it. */
	mboot_struct = mmu_map_from_physical((uintptr_t)mboot);

	/* memCount and maxAddress come from multiboot data */
	mmu_init(highest_valid_address, highest_kernel_address);

	/* With the MMU initialized, set up things required for the scheduler. */
	pat_initialize();
	symbols_install();
	gdt_install();
	idt_install();
	fpu_initialize();
	pic_initialize();

	/* Early generic stuff */
	generic_startup();

	/* Should we override the TSC timing? */
	if (args_present("tsc_mhz")) {
		extern unsigned long tsc_mhz;
		tsc_mhz = atoi(args_value("tsc_mhz"));
	}

	/* Scheduler is running and we have parsed the kcmdline, initialize video. */
	framebuffer_initialize();
	fbterm_initialize();

	smp_initialize();

	/* Decompress and mount all initial ramdisks. */
	mount_multiboot_ramdisks(mboot);

	/* Set up preempt source */
	pit_initialize();

	/* Install generic PC device drivers. */
	ps2hid_install();
	serial_initialize();
	portio_initialize();

	/* Yield to the generic main, which starts /bin/init */
	return generic_main();
}
