#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"
#include "text.h"
#include "util.h"
#include "menu.h"
#include "elf.h"
#include "options.h"
#include "iso9660.h"

char * kernel_load_start = 0;

mboot_mod_t modules_mboot[1] = {
	{0,0,0,1}
};

struct multiboot multiboot_header = {
	/* flags;             */ MULTIBOOT_FLAG_CMDLINE | MULTIBOOT_FLAG_MODS | MULTIBOOT_FLAG_MEM | MULTIBOOT_FLAG_MMAP | MULTIBOOT_FLAG_LOADER,
	/* mem_lower;         */ 0x100000,
	/* mem_upper;         */ 0x640000,
	/* boot_device;       */ 0,
	/* cmdline;           */ 0,
	/* mods_count;        */ 1,
	/* mods_addr;         */ 0,
	/* num;               */ 0,
	/* size;              */ 0,
	/* addr;              */ 0,
	/* shndx;             */ 0,
	/* mmap_length;       */ 0,
	/* mmap_addr;         */ 0,
	/* drives_length;     */ 0,
	/* drives_addr;       */ 0,
	/* config_table;      */ 0,
	/* boot_loader_name;  */ 0,
	/* apm_table;         */ 0,
	/* vbe_control_info;  */ 0,
	/* vbe_mode_info;     */ 0,
	/* vbe_mode;          */ 0,
	/* vbe_interface_seg; */ 0,
	/* vbe_interface_off; */ 0,
	/* vbe_interface_len; */ 0,
};

static uintptr_t ramdisk_off = 0;
static uintptr_t ramdisk_len = 0;

uint32_t _eax = 1;
uint32_t _ebx = 1;
uint32_t _xmain = 1;

struct mmap_entry {
	uint64_t base;
	uint64_t len;
	uint32_t type;
	uint32_t reserved;
};

extern unsigned short mmap_ent;
extern unsigned short lower_mem;

uintptr_t final_offset = 0;

static int load_kernel(void) {
	clear();
	Elf32_Header * header = (Elf32_Header *)kernel_load_start;

	if (header->e_ident[0] != ELFMAG0 ||
	    header->e_ident[1] != ELFMAG1 ||
	    header->e_ident[2] != ELFMAG2 ||
	    header->e_ident[3] != ELFMAG3) {
		print_("Not a valid ELF32.\n");
		return 0;
	}

	uintptr_t entry = (uintptr_t)header->e_entry;
	for (uintptr_t x = 0; x < (uint32_t)header->e_phentsize * header->e_phnum; x += header->e_phentsize) {
		Elf32_Phdr * phdr = (Elf32_Phdr *)(kernel_load_start + header->e_phoff + x);
		if (phdr->p_type == PT_LOAD) {
			memcpy((uint8_t*)(uintptr_t)phdr->p_vaddr, kernel_load_start + phdr->p_offset, phdr->p_filesz);
			uintptr_t r = phdr->p_filesz;
			while (r < phdr->p_memsz) {
				*(char *)(phdr->p_vaddr + r) = 0;
				r++;
			}
			if (phdr->p_vaddr + r > final_offset) final_offset = phdr->p_vaddr + r;
		}
	}

	_xmain = entry;

	/* Round final offset */
	final_offset = (final_offset & ~(0xFFF)) + ((final_offset & 0xFFF) ? 0x1000 : 0);

	print_("Loaded with end at 0x"); print_hex_(final_offset); print_("\n");
	return 1;
}

static void relocate_ramdisk(void) {
	char * dest = (char*)final_offset;
	char * src  = (char*)ramdisk_off;
	for (size_t s = 0; s < ramdisk_len; ++s) {
		dest[s] = src[s];
	}

	modules_mboot[0].mod_start = final_offset;
	modules_mboot[0].mod_end   = final_offset + ramdisk_len;

	final_offset += ramdisk_len;
	final_offset = (final_offset & ~(0xFFF)) + ((final_offset & 0xFFF) ? 0x1000 : 0);
}

static void finish_boot(void) {
	print("Setting up memory map...\n");
	print_hex(mmap_ent);
	print("\n");
	memset(kernel_load_start, 0x00, 1024);
	mboot_memmap_t * mmap = (void*)final_offset;
	multiboot_header.mmap_addr = (uintptr_t)mmap;
	multiboot_header.mods_addr = (uintptr_t)&modules_mboot;
	multiboot_header.boot_loader_name = (uintptr_t)VERSION_TEXT;

	struct mmap_entry * e820 = (void*)0x5000;

	uint64_t upper_mem = 0;
	for (int i = 0; i < mmap_ent; ++i) {
		print("entry "); print_hex(i);
		print(" "); print_hex((uint32_t)(e820[i].base >> 32ULL)); print_hex((uint32_t)e820[i].base);
		print(" "); print_hex((uint32_t)(e820[i].len >> 32ULL)); print_hex((uint32_t)e820[i].len);
		print(" "); print_hex(e820[i].type); print("\n");

		mmap->size = sizeof(uint64_t) * 2 + sizeof(uintptr_t);
		mmap->base_addr = e820[i].base;
		mmap->length = e820[i].len;
		mmap->type = e820[i].type;
		if (mmap->type == 1 && mmap->base_addr >= 0x100000) {
			upper_mem += mmap->length;
		}
		mmap = (mboot_memmap_t *) ((uintptr_t)mmap + mmap->size + sizeof(uintptr_t));
	}
	multiboot_header.mmap_length = (uintptr_t)mmap - multiboot_header.mmap_addr;

	print("lower "); print_hex(lower_mem); print("KB\n");
	multiboot_header.mem_lower = 1024;
	print("upper ");
	print_hex(upper_mem >> 32);
	print_hex(upper_mem);
	print("\n");
	
	multiboot_header.mem_upper = upper_mem / 1024;

	_ebx = (unsigned int)&multiboot_header;
	_eax = MULTIBOOT_EAX_MAGIC;

	print_("Jumping...\n");

	uint32_t foo[3];
	foo[0] = _eax;
	foo[1] = _ebx;
	foo[2] = _xmain;
	__asm__ __volatile__ (
		"mov %%cr0,%%eax\n"
		/* Disable paging */
		"and $0x7FFeFFFF, %%eax\n"
		"mov %%eax,%%cr0\n"
		/* Ensure PAE is not enabled */
		"mov %%cr4,%%eax\n"
		"and $0xffffffdf, %%eax\n"
		"mov %%eax,%%cr4\n"
		"mov %1,%%eax \n"
		"mov %2,%%ebx \n"
		"jmp *%0" : : "g"(foo[2]), "g"(foo[0]), "g"(foo[1]) : "eax", "ebx"
		);
}

void boot(void) {
	clear_();
	print_("Looking for ISO9660 filesystem... ");
	for (int i = 0x10; i < 0x15; ++i) {
		root = (void*)(DATA_LOAD_BASE + ISO_SECTOR_SIZE * i);
		switch (root->type) {
			case 1:
				print_("found.\n");
				goto done;
		}
	}
	return;
done:

	print_("Looking for kernel... ");
	if (!navigate(kernel_path)) {
		print_("Failed to locate kernel.\n");
		return;
	}
	print_("found.\n");

	kernel_load_start = (char*)(DATA_LOAD_BASE + dir_entry->extent_start_LSB * ISO_SECTOR_SIZE);

	print_("Looking for ramdisk... ");
	if (!navigate(ramdisk_path)) {
		print_("Failed to locate ramdisk.\n");
		return;
	}
	print_("found.\n");

	ramdisk_off = DATA_LOAD_BASE + dir_entry->extent_start_LSB * ISO_SECTOR_SIZE;
	ramdisk_len = dir_entry->extent_length_LSB;

	multiboot_header.cmdline = (uintptr_t)cmdline;

	print_("Loading kernel from 0x"); print_hex_((uint32_t)kernel_load_start); print_("... ");
	if (!load_kernel()) {
		print_("Failed to load kernel.\n");
		return;
	}

	print_("Relocating ramdisk from 0x"); print_hex_((uint32_t)ramdisk_off); print_(":0x"); print_hex_(ramdisk_len); print_(" to 0x"); print_hex_(final_offset); print_("... ");
	relocate_ramdisk();
	finish_boot();
}

