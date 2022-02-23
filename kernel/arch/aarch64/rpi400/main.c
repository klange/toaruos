/**
 * @file  kernel/arch/aarch64/rpi400/main.c
 * @brief Boot stub for Raspberry Pi 400.
 *
 * This gets built into kernel8.img, which embeds the actual kernel and
 * a compress ramdisk. The bootstub is responsible for acquiring the
 * initial framebuffer, setting the cores to max speed, setting up the
 * MMU, and loading the actual kernel at -2GiB.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2022 K. Lange
 */
#include <stdint.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/elf.h>

#include <kernel/arch/aarch64/rpi.h>

#define MMIO_BASE 0xFE000000UL
#define MBOX_BASE    (MMIO_BASE + 0xB880)
#define MBOX_READ    (MBOX_BASE + 0x00)
#define MBOX_STATUS  (MBOX_BASE + 0x18)
#define MBOX_WRITE   (MBOX_BASE + 0x20)
#define MBOX_FULL    0x80000000
#define MBOX_EMPTY   0x40000000
#define MBOX_RESPONSE 0x80000000
#define MBOX_REQUEST  0

volatile uint32_t __attribute__((aligned(16))) mbox[36];

static uint32_t mmio_read32(uintptr_t addr) {
	uint32_t res = *((volatile uint32_t*)(addr));
	return res;
}
static void mmio_write32(uintptr_t addr, uint32_t val) {
	(*((volatile uint32_t*)(addr))) = val;
}

uint32_t mbox_call(uint8_t ch) {
	uint32_t r = ((uint32_t)((uintptr_t)&mbox) & ~0xF) | (ch & 0xF);

	while (mmio_read32(MBOX_STATUS) == MBOX_FULL); /* wait for mailbox to be ready */
	mmio_write32(MBOX_WRITE, r);

	while (1) {
		while (mmio_read32(MBOX_STATUS) & MBOX_EMPTY);
		if (r == mmio_read32(MBOX_READ)) return mbox[1] == MBOX_RESPONSE;
	}
}

uint8_t * lfb_vid_memory = 0;
uint16_t lfb_resolution_x = 0;
uint16_t lfb_resolution_y = 0;
uint16_t lfb_resolution_b = 0;
uint32_t lfb_resolution_s = 0;
size_t lfb_memsize = 0;

void * malloc(size_t x) {
	while (1);
}

#define MB(j) mbox[i++] = j
int rpi_fb_init(void) {
	int i = 0;

	MB(35 * 4);
	MB(MBOX_REQUEST);

	MB(0x48003);
	MB(8);
	MB(0);
	int fb_width = i;
	MB(1920);
	int fb_height = i;
	MB(1080);

	MB(0x48004);
	MB(8);
	MB(8);
	MB(1920);
	MB(1080);

	MB(0x48009);
	MB(8);
	MB(8);
	MB(0);
	MB(0);

	MB(0x48005);
	MB(4);
	MB(4);
	int fb_bpp = i;
	MB(32);

	MB(0x48006);
	MB(4);
	MB(4);
	MB(1);

	MB(0x40001);
	MB(8);
	MB(8);
	int fb_pointer = i;
	MB(4096);
	int fb_size = i;
	MB(0);

	MB(0x40008);
	MB(4);
	MB(4);
	int fb_pitch = i;
	MB(0);

	MB(0);

	if (mbox_call(8) && mbox[fb_bpp] == 32 && mbox[fb_pointer] != 0) {
		lfb_vid_memory = (uint8_t*)(uintptr_t)(mbox[fb_pointer] & 0x3FFFFFFF);
		lfb_resolution_x = mbox[fb_width];
		lfb_resolution_y = mbox[fb_height];
		lfb_resolution_s = mbox[fb_pitch];
		lfb_resolution_b = mbox[fb_bpp];
		lfb_memsize = mbox[fb_size];

		for (unsigned int y = 0; y < lfb_resolution_y; ++y) {
			for (unsigned int x = 0; x < lfb_resolution_x; ++x) {
				*(volatile uint32_t *)((uintptr_t)lfb_vid_memory + y * lfb_resolution_s + x * 4) = 0x3ea3f0;
			}
		}
		extern void fbterm_initialize(void);
		fbterm_initialize();

		return 0;
	}

	return 1;
}

void rpi_cpu_freq(void) {
	int max_rate = 0;
	{
		int i = 0;
		MB(13 * 4);
		MB(MBOX_REQUEST);

		MB(0x30004);
		MB(8);
		MB(0);
		MB(3); /* arm core */
		int max_hz = i;
		MB(0);

		MB(0x30047);
		MB(8);
		MB(0);
		MB(3); /* arm core */
		int cur_hz = i;
		MB(0);

		MB(0);

		mbox_call(8);

		printf("bootstub: max clock rate is %u Hz, current is %u Hz\n", mbox[max_hz], mbox[cur_hz]);
		max_rate = mbox[max_hz];
	}

	if (max_rate) {
		int i = 0;
		MB(9 * 4);
		MB(MBOX_REQUEST);

		MB(0x38002);
		MB(12);
		MB(0);
		MB(3);
		int rate = i;
		MB(max_rate);
		MB(0); /* do not skip turbo setting */

		MB(0);

		mbox_call(8);
		printf("bootstub: clock rate set to %u Hz\n", mbox[rate]);
	}
}

extern char _kernel_start[];
extern char _kernel_end[];
extern char _ramdisk_start[];
extern char _ramdisk_end[];

static struct BaseTables {
	uintptr_t l0_base[512];
	uintptr_t l1_high_gbs[512];
	uintptr_t l1_low_gbs[512];
	uintptr_t l2_kernel[512];
} _baseTables __attribute__((aligned(4096)));

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


#define KERNEL_PHYS_BASE 0x2000000UL

static void bootstub_mmu_init(void) {
	/* Map memory */
	_baseTables.l0_base[0]   = (uintptr_t)&_baseTables.l1_low_gbs | PTE_VALID | PTE_TABLE | PTE_AF;

	/* equivalent to high_base_pml */
	_baseTables.l0_base[511] = (uintptr_t)&_baseTables.l1_high_gbs | PTE_VALID | PTE_TABLE | PTE_AF;

	/* Mapping for us */
	_baseTables.l1_low_gbs[0] = 0x00000000UL | PTE_VALID | PTE_AF | PTE_SH_A | (1 << 2);
	_baseTables.l1_low_gbs[1] = 0x40000000UL | PTE_VALID | PTE_AF | PTE_SH_A | (1 << 2);
	_baseTables.l1_low_gbs[2] = 0x80000000UL | PTE_VALID | PTE_AF | PTE_SH_A | (1 << 2);
	_baseTables.l1_low_gbs[3] = 0xc0000000UL | PTE_VALID | PTE_AF | PTE_SH_A | (1 << 2);

	/* -512GB is a map of 64GB of memory */
	for (size_t i = 0; i < 64; ++i) {
		_baseTables.l1_high_gbs[i] = (i << 30) | PTE_VALID | PTE_AF | PTE_SH_A | (1 << 2);
	}

	/* -2GiB, map kernel here */
	_baseTables.l1_high_gbs[510] = (uintptr_t)&_baseTables.l2_kernel | PTE_VALID | PTE_TABLE | PTE_AF;

	for (size_t i = 0; i < 512; ++i) {
		_baseTables.l2_kernel[i] = (KERNEL_PHYS_BASE + (i << 21)) | PTE_VALID | PTE_AF | PTE_SH_A | (1 << 2);
	}


	uint64_t sctlr = 0
		| (1UL << 0)  /* mmu enabled */
		| (1UL << 2)  /* cachability */
		//| (1UL << 6)
		| (1UL << 12) /* instruction cachability */
		| (1UL << 23) /* SPAN */
		| (1UL << 28) /* nTLSMD */
		| (1UL << 29) /* LSMAOE */
		| (1UL << 20) /* TSCXT */
		| (1UL << 7)  /* ITD */
	;

	/* Translate control register */
	uint64_t tcr = 0
		|  (3UL << 32)  /* 36 bits? */
		|  (2UL << 30) /* TG1 4KB granules in TTBR1 */
		| (16UL << 16) /* T1SZ 48-bit */
		|  (3UL << 28)  /* SH1 */
		|  (1UL << 26)  /* ORGN1 */
		|  (1UL << 24)  /* IRGN1 */
		|  (0UL << 14) /* TG0 4KB granules in TTBR0 */
		| (16UL <<  0)  /* T0SZ 48-bit */
		|  (3UL << 12)  /* SH0 */
		|  (1UL << 10)  /* ORGN0 */
		|  (1UL <<  8)   /* IRGN0 */
	;

	/* MAIR setup? */
	uint64_t mair  = (0x000000000044ff00);
	asm volatile ("msr MAIR_EL1,%0" :: "r"(mair));

	/* Frob bits */
	printf("bootstub: setting base values\n");
	asm volatile ("msr TCR_EL1,%0" : : "r"(tcr));
	asm volatile ("msr TTBR0_EL1,%0" : : "r"(&_baseTables.l0_base));
	asm volatile ("msr TTBR1_EL1,%0" : : "r"(&_baseTables.l0_base));
	printf("bootstub: frobbing bits\n");
	asm volatile ("dsb ishst\ntlbi vmalle1is\ndsb ish\nisb" ::: "memory");
	printf("bootstub: enabling mmu\n");
	asm volatile ("msr SCTLR_EL1,%0" : : "r"(sctlr));
	asm volatile ("isb" ::: "memory");

	printf("bootstub: MMU initialized\n");

}

static void bootstub_load_kernel(Elf64_Header * header) {
	/* Find load headers */
	for (int i = 0; i < header->e_phnum; ++i) {
		Elf64_Phdr * phdr = (void*)((uintptr_t)header + (header->e_phoff + header->e_phentsize * i));
		if (phdr->p_type == PT_LOAD) {
			printf("bootstub: Load %zu bytes @ %zx from off %zx\n", phdr->p_memsz, phdr->p_vaddr, phdr->p_offset);
			memset((void*)phdr->p_vaddr, 0, phdr->p_memsz);
			memcpy((void*)phdr->p_vaddr, (void*)((uintptr_t)header + phdr->p_offset), phdr->p_filesz);
		} else {
			printf("bootstub: Skip phdr %d\n", i);
		}
	}
}

struct rpitag tag_data = {0};

static void bootstub_start_kernel(uintptr_t dtb, Elf64_Header * header) {
	printf("bootstub: Jump to kernel entry point at %zx\n",
		header->e_entry);

	void (*entry)(uintptr_t,uintptr_t,uintptr_t) = (void(*)(uintptr_t,uintptr_t,uintptr_t))header->e_entry;
	entry(dtb, KERNEL_PHYS_BASE, (uintptr_t)&tag_data);
}

static void bootstub_exit_el2(void) {
	uint64_t spsr_el2, sctlr_el1;
	asm volatile ("mrs %0, SPSR_EL2\n" :"=r"(spsr_el2));
	printf("bootstub: SPSR_EL2=%#zx\n", spsr_el2);

	asm volatile ("mrs %0, SCTLR_EL1\n" :"=r"(sctlr_el1));
	printf("bootstub: SCTLR_EL1=%#zx\n", sctlr_el1);

	/* get us out of EL2 */

	asm volatile (
		"ldr x0, =0x1004\n"
		"mrs x1, SCTLR_EL2\n"
		"orr x1, x1, x0\n"
		"msr SCTLR_EL2, x1\n"
		"ldr x0, =0x30d01804\n"
		"msr SCTLR_EL1, x0\n" ::: "x0", "x1");

	printf("bootstub: sctlr_el1 set\n");

	asm volatile (
		"ldr x0, =0x80000000\n"
		"msr HCR_EL2, x0\n" ::: "x0");

	printf("bootstub: hcr set\n");

	#if 0
	asm volatile (
		"ldr x0, =0x431\n"
		"msr SCR_EL3, x0\n" ::: "x0");
	printf("bootstub: SCR_EL3 set\n");
	#endif

	asm volatile (
		"ldr x0, =0x3c5\n"
		"msr SPSR_EL2, x0\n" ::: "x0");

	printf("bootstub: spsr_el2 set\n");

	asm volatile (
		"mov x0, sp\n"
		"msr SP_EL1, x0\n"
		"adr x0, in_el1\n"
		"msr ELR_EL2, x0\n"
		"eret\n"
		"in_el1:\n"
		::: "x0", "memory", "cc"
	);

	printf("bootstub: out of EL2?\n");

	uint64_t CurrentEL;
	asm volatile ("mrs %0, CurrentEL" : "=r"(CurrentEL));
	printf("in el%zu\n", CurrentEL >> 2);
}

void kmain(uint32_t dtb_address, uint32_t base_addr) {

	if (rpi_fb_init()) {
		/* Panic */
		while (1);
	}

	printf("rpi4 bootstub, kernel base address is %#x, dtb is at %#x\n", base_addr, dtb_address);

	printf("framebuffer (%u x %u) @ %#zx\n",
		lfb_resolution_x,
		lfb_resolution_y,
		(uintptr_t)lfb_vid_memory);

	uint64_t CurrentEL;
	asm volatile ("mrs %0, CurrentEL" : "=r"(CurrentEL));
	printf("in el%zu\n", CurrentEL >> 2);

	printf("kernel @ %#zx (%zu bytes) ramdisk @ %#zx (%zu bytes)\n",
		(uintptr_t)&_kernel_start,
		(size_t)((uintptr_t)&_kernel_end - (uintptr_t)&_kernel_start),
		(uintptr_t)&_ramdisk_start,
		(size_t)((uintptr_t)&_ramdisk_end - (uintptr_t)&_ramdisk_start));

	rpi_cpu_freq();

	bootstub_exit_el2();

	bootstub_mmu_init();

	tag_data.phys_addr = lfb_vid_memory;
	tag_data.x = lfb_resolution_x;
	tag_data.y = lfb_resolution_y;
	tag_data.s = lfb_resolution_s;
	tag_data.b = lfb_resolution_b;
	tag_data.size = lfb_memsize;
	tag_data.ramdisk_start = (uintptr_t)&_ramdisk_start;
	tag_data.ramdisk_end   = (uintptr_t)&_ramdisk_end;

	Elf64_Header *header = (void*)&_kernel_start;
	bootstub_load_kernel(header);

	/* Jump to kernel */
	bootstub_start_kernel(dtb_address, header);

	while (1);

}
