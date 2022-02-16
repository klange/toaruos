/**
 * @file  kernel/arch/aarch64/bootstub/main.c
 * @brief Shim loader for QEMU virt machine.
 *
 * Loads at 0x4010_0000 where RAM is, sets up the MMU to have RAM
 * at our kernel virtual load address (0xffff_ffff_8000_0000), as
 * well as a direct mapping at -512GB for access to IO devices,
 * reads the kernel out of fw-cfg, loads it to the kernel virtual
 * load address, and then jumps to it.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2022 K. Lange
 */
#include <stdint.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/elf.h>

#define QEMU_DTB_BASE     0x40000000UL
#define KERNEL_PHYS_BASE  0x41000000UL

static uint32_t swizzle(uint32_t from) {
	uint8_t a = from >> 24;
	uint8_t b = from >> 16;
	uint8_t c = from >> 8;
	uint8_t d = from;
	return (d << 24) | (c << 16) | (b << 8) | (a);
}

void * malloc(size_t x) {
	printf("panic\n");
	while (1);
}

static uint64_t swizzle64(uint64_t from) {
	uint8_t a = from >> 56;
	uint8_t b = from >> 48;
	uint8_t c = from >> 40;
	uint8_t d = from >> 32;
	uint8_t e = from >> 24;
	uint8_t f = from >> 16;
	uint8_t g = from >> 8;
	uint8_t h = from;
	return ((uint64_t)h << 56) | ((uint64_t)g << 48) | ((uint64_t)f << 40) | ((uint64_t)e << 32) | (d << 24) | (c << 16) | (b << 8) | (a);
}

static uint16_t swizzle16(uint16_t from) {
	uint8_t a = from >> 8;
	uint8_t b = from;
	return (b << 8) | (a);
}

struct fdt_header {
	uint32_t magic;
	uint32_t totalsize;
	uint32_t off_dt_struct;
	uint32_t off_dt_strings;
	uint32_t off_mem_rsvmap;
	uint32_t version;
	uint32_t last_comp_version;
	uint32_t boot_cpuid_phys;
	uint32_t size_dt_strings;
	uint32_t size_dt_struct;
};

static uint32_t * parse_node(uint32_t * node, char * strings, int x) {
	while (swizzle(*node) == 4) node++;
	if (swizzle(*node) == 9) return NULL;
	if (swizzle(*node) != 1) {
		printf("Not a node? Got %x\n", swizzle(*node));
		return NULL;
	}

	/* Skip the BEGIN_NODE */
	node++;

	for (int i = 0; i < x; ++i) printf("  ");

	while (1) {
		char * x = (char*)node;
		if (x[0]) { printf("%c",x[0]); } else { node++; break; }
		if (x[1]) { printf("%c",x[1]); } else { node++; break; }
		if (x[2]) { printf("%c",x[2]); } else { node++; break; }
		if (x[3]) { printf("%c",x[3]); } else { node++; break; }
		node++;
	}
	printf("\n");

	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			for (int i = 0; i < x; ++i) printf("  ");
			uint32_t len = swizzle(node[1]);
			uint32_t nameoff = swizzle(node[2]);
			printf("  property %s len=%u\n", strings + nameoff, len);
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			node = parse_node(node, strings, x + 1);
		}
	}

}

static void dump_dtb(uintptr_t addr) {

	struct fdt_header * fdt = (struct fdt_header*)addr;

#define P(o) printf(#o " = %#x\n", swizzle(fdt-> o))
	P(magic);
	P(totalsize);
	P(off_dt_struct);
	P(off_dt_strings);
	P(off_mem_rsvmap);
	P(version);
	P(last_comp_version);
	P(boot_cpuid_phys);
	P(size_dt_strings);
	P(size_dt_struct);

	char * dtb_strings = (char *)(addr + swizzle(fdt->off_dt_strings));
	uint32_t * dtb_struct = (uint32_t *)(addr + swizzle(fdt->off_dt_struct));

	parse_node(dtb_struct, dtb_strings, 0);
}

static uint32_t * find_subnode(uint32_t * node, char * strings, const char * name, uint32_t ** node_out, int (*cmp)(const char* a, const char *b)) {
	while (swizzle(*node) == 4) node++;
	if (swizzle(*node) == 9) return NULL;
	if (swizzle(*node) != 1) return NULL;
	node++;

	if (cmp((char*)node,name)) {
		*node_out = node;
		return NULL;
	}

	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF)) node++;
	node++;

	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			uint32_t len = swizzle(node[1]);
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			node = find_subnode(node, strings, name, node_out, cmp);
			if (!node) return NULL;
		}
	}
}

static uint32_t * find_node_int(const char * name, int (*cmp)(const char*,const char*)) {
	uintptr_t addr = QEMU_DTB_BASE;
	struct fdt_header * fdt = (struct fdt_header*)addr;
	char * dtb_strings = (char *)(addr + swizzle(fdt->off_dt_strings));
	uint32_t * dtb_struct = (uint32_t *)(addr + swizzle(fdt->off_dt_struct));

	uint32_t * out = NULL;
	find_subnode(dtb_struct, dtb_strings, name, &out, cmp);
	return out;
}

static int base_cmp(const char *a, const char *b) {
	return !strcmp(a,b);
}
static uint32_t * find_node(const char * name) {
	return find_node_int(name,base_cmp);
}

static int prefix_cmp(const char *a, const char *b) {
	return !memcmp(a,b,strlen(b));
}

static uint32_t * find_node_prefix(const char * name) {
	return find_node_int(name,prefix_cmp);
}

static uint32_t * node_find_property_int(uint32_t * node, char * strings, const char * property, uint32_t ** out) {
	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF)) node++;
	node++;

	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			uint32_t len = swizzle(node[1]);
			uint32_t nameoff = swizzle(node[2]);
			if (!strcmp(strings + nameoff, property)) {
				*out = &node[1];
				return NULL;
			}
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			node = node_find_property_int(node+1, strings, property, out);
			if (!node) return NULL;
		}
	}
}

static uint32_t * node_find_property(uint32_t * node, const char * property) {
	uintptr_t addr = QEMU_DTB_BASE;
	struct fdt_header * fdt = (struct fdt_header*)addr;
	char * dtb_strings = (char *)(addr + swizzle(fdt->off_dt_strings));
	uint32_t * out = NULL;
	node_find_property_int(node, dtb_strings, property, &out);
	return out;
}

static size_t _early_log_write(size_t size, uint8_t * buffer) {
	for (unsigned int i = 0; i < size; ++i) {
		*(volatile unsigned int *)(0x09000000) = buffer[i];
	}
	return size;
}

static size_t _later_log_write(size_t size, uint8_t * buffer) {
	for (unsigned int i = 0; i < size; ++i) {
		*(volatile unsigned int *)(0xffffff8009000000) = buffer[i];
	}
	return size;
}

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

static void bootstub_mmu_init(void) {
	/* Map memory */
	_baseTables.l0_base[0]   = (uintptr_t)&_baseTables.l1_low_gbs | PTE_VALID | PTE_TABLE | PTE_AF;

	/* equivalent to high_base_pml */
	_baseTables.l0_base[511] = (uintptr_t)&_baseTables.l1_high_gbs | PTE_VALID | PTE_TABLE | PTE_AF;

	/* Mapping for us */
	_baseTables.l1_low_gbs[1] = QEMU_DTB_BASE | PTE_VALID | PTE_AF | PTE_SH_A | (1 << 2);

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
		|  (3UL << 32)  /* IPS 4TB? */
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
	asm volatile ("msr SCTLR_EL1,%0" : : "r"(sctlr));
	asm volatile ("isb" ::: "memory");

	/* Point log output at new mmio address */
	printf_output = &_later_log_write;

	printf("bootstub: MMU initialized\n");
}

static void bootstub_read_kernel(uintptr_t kernel_load_addr) {
	/* See if we can find a qemu fw_cfg interface, we can use that for a ramdisk */
	uint32_t * fw_cfg = find_node_prefix("fw-cfg");
	if (fw_cfg) {
		printf("bootstub: found fw-cfg interface\n");
		/* best guess until we bother parsing these */
		uint32_t * regs = node_find_property(fw_cfg, "reg");
		if (regs) {
			printf("bootstub:   length of regs = %u\n", swizzle(regs[0]));
			printf("bootstub:   addr of fw-cfg = %#x\n", swizzle(regs[3]));

			volatile uint8_t * fw_cfg_addr = (volatile uint8_t*)(uintptr_t)(swizzle(regs[3]) + 0xffffff8000000000);
			volatile uint64_t * fw_cfg_data = (volatile uint64_t *)fw_cfg_addr;
			volatile uint32_t * fw_cfg_32   = (volatile uint32_t *)fw_cfg_addr;
			volatile uint16_t * fw_cfg_sel  = (volatile uint16_t *)(fw_cfg_addr + 8);

			*fw_cfg_sel = 0;

			uint64_t response = fw_cfg_data[0];

			printf("bootstub: response: %c%c%c%c\n",
				(char)(response >> 0),
				(char)(response >> 8),
				(char)(response >> 16),
				(char)(response >> 24));

			/* Needs to be big-endian */
			*fw_cfg_sel = swizzle16(0x19);

			/* count response is 32-bit BE */
			uint32_t count = swizzle(fw_cfg_32[0]);
			printf("bootstub: %u entries\n", count);

			struct fw_cfg_file {
				uint32_t size;
				uint16_t select;
				uint16_t reserved;
				char name[56];
			};

			struct fw_cfg_file file;
			uint8_t * tmp = (uint8_t *)&file;

			/* Read count entries */
			for (unsigned int i = 0; i < count; ++i) {
				for (unsigned int j = 0; j < sizeof(struct fw_cfg_file); ++j) {
					tmp[j] = fw_cfg_addr[0];
				}

				/* endian swap to get file size and selector ID */
				file.size = swizzle(file.size);
				file.select = swizzle16(file.select);

				printf("bootstub: 0x%04x %s (%d bytes)\n",
					file.select, file.name, file.size);

				if (!strcmp(file.name,"opt/org.toaruos.kernel")) {
					printf("bootstub: Found kernel, loading\n");
					uint8_t * x = (uint8_t*)kernel_load_addr;

					struct fwcfg_dma {
						volatile uint32_t control;
						volatile uint32_t length;
						volatile uint64_t address;
					} dma;

					dma.control = swizzle((file.select << 16) | (1 << 3) | (1 << 1));
					dma.length  = swizzle(file.size);
					dma.address = swizzle64((uintptr_t)x);

					fw_cfg_data[2] = swizzle64((uint64_t)&dma);

					if (dma.control) {
						printf("bootstub: error on dma read?\n");
						return;
					}

					return;
				}
			}
		}
	}

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

static void bootstub_start_kernel(Elf64_Header * header) {
	printf("bootstub: Jump to kernel entry point at %zx\n",
		header->e_entry);

	void (*entry)(uintptr_t,uintptr_t,uintptr_t) = (void(*)(uintptr_t,uintptr_t,uintptr_t))header->e_entry;
	entry(QEMU_DTB_BASE, KERNEL_PHYS_BASE, 0);
}

int kmain(void) {
	extern char end[];
	uintptr_t kernel_load_addr = (uintptr_t)&end;

	/* Initialize log */
	printf_output = &_early_log_write;
	printf("bootstub: Starting up\n");

	/* Set up MMU */
	bootstub_mmu_init();

	/* Read the kernel from fw-cfg */
	bootstub_read_kernel(kernel_load_addr);

	/* Examine kernel */
	Elf64_Header *header = (void*)kernel_load_addr;
	bootstub_load_kernel(header);

	/* Jump to kernel */
	bootstub_start_kernel(header);

	while (1) {}
	return 0;
}
