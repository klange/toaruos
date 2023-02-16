/**
 * @brief Main bootloader logic.
 *
 * Does all the heavy lifting after configuration options have
 * been selected by the user. Loads the kernel and ramdisk,
 * sets up multiboot structures, and jumps to the kernel.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2021 K. Lange
 */
#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"
#include "text.h"
#include "util.h"
#include "menu.h"
#include "elf.h"
#include "options.h"
#include "iso9660.h"
#include "kbd.h"

extern void draw_logo(int);
char * kernel_load_start = 0;

mboot_mod_t modules_mboot[1] = {
	{0,0,0,1}
};

static struct multiboot multiboot_header = {
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
uintptr_t final_offset = 0;
uintptr_t _xmain = 0;

static int load_aout(uint32_t * hdr) {
	uintptr_t base_offset = (uintptr_t)hdr - (uintptr_t)kernel_load_start;
	uintptr_t hdr_offset  = hdr[3] - base_offset;

	memcpy((void*)(uintptr_t)hdr[4], kernel_load_start + (hdr[4] - hdr_offset), (hdr[5] - hdr[4]));
	memset((void*)(uintptr_t)hdr[5], 0, (hdr[6] - hdr[5]));
	_xmain = (uintptr_t)hdr[7];

	if (hdr[6] > final_offset) final_offset = hdr[6];
	final_offset = (final_offset & ~(0xFFF)) + ((final_offset & 0xFFF) ? 0x1000 : 0);

	return 1;
}

static int load_elf32(Elf32_Header * header) {
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

	print("Loaded with end at 0x"); print_hex(final_offset); print("\n");
	return 1;
}

static int load_kernel(void) {
	clear();

	/* Check for Multiboot header */
	for (uintptr_t x = 0; x < 8192; x += 4) {
		uint32_t * check = (uint32_t *)(kernel_load_start + x);
		if (*check == 0x1BADB002) {
			if (check[1] & (1 << 16)) {
				return load_aout(check);
			} else {
				return load_elf32((void*)kernel_load_start);
			}
		}
	}

	return 0;
}

static void relocate_ramdisk(mboot_mod_t * mboot_mods) {
	char * dest = (char*)final_offset;
	char * src  = (char*)ramdisk_off;
	for (size_t s = 0; s < ramdisk_len; ++s) {
		dest[s] = src[s];
	}

	mboot_mods->mod_start = final_offset;
	mboot_mods->mod_end   = final_offset + ramdisk_len;

	final_offset += ramdisk_len;
	final_offset = (final_offset & ~(0xFFF)) + ((final_offset & 0xFFF) ? 0x1000 : 0);
}

#ifdef EFI_PLATFORM
#include <efi.h>
extern EFI_GRAPHICS_OUTPUT_PROTOCOL * GOP;

/* EFI boot uses simple filesystem driver */
static EFI_GUID efi_simple_file_system_protocol_guid =
	{0x0964e5b22,0x6459,0x11d2,0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b};

static EFI_GUID efi_loaded_image_protocol_guid =
	{0x5B1B31A1,0x9562,0x11d2, {0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}};

extern EFI_SYSTEM_TABLE *ST;
extern EFI_HANDLE ImageHandleIn;

extern char do_the_nasty[];
static void finish_boot(void) {
	/* Set up multiboot header */
	struct multiboot * finalHeader = (void*)(uintptr_t)final_offset;
	memcpy((void*)final_offset, &multiboot_header, sizeof(struct multiboot));
	final_offset += sizeof(struct multiboot);

	finalHeader->flags |= MULTIBOOT_FLAG_FB;
	finalHeader->framebuffer_addr = GOP->Mode->FrameBufferBase;
	finalHeader->framebuffer_pitch = GOP->Mode->Info->PixelsPerScanLine * 4;
	finalHeader->framebuffer_width = GOP->Mode->Info->HorizontalResolution;
	finalHeader->framebuffer_height = GOP->Mode->Info->VerticalResolution;
	finalHeader->framebuffer_bpp  = 32;

	/* Copy in command line */
	memcpy((void*)final_offset, cmdline, strlen(cmdline)+1);
	finalHeader->cmdline = (uintptr_t)final_offset;
	final_offset += strlen(cmdline) + 1;

	/* Copy bootloader name */
	memcpy((void*)final_offset, VERSION_TEXT, strlen(VERSION_TEXT)+1);
	finalHeader->boot_loader_name = (uintptr_t)final_offset;
	final_offset += strlen(VERSION_TEXT) + 1;

	/* Copy module pointers */
	memcpy((void*)final_offset, modules_mboot, sizeof(modules_mboot));
	finalHeader->mods_addr = (uintptr_t)final_offset;
	final_offset += sizeof(modules_mboot);

	/* Realign for memory map */
	final_offset = (final_offset & ~(0xFFF)) + ((final_offset & 0xFFF) ? 0x1000 : 0);

	/* Write memory map */
	mboot_memmap_t * mmap = (void*)final_offset;
	memset((void*)final_offset, 0x00, 1024);
	finalHeader->mmap_addr = (uint32_t)(uintptr_t)mmap;

	{
		EFI_STATUS e;
		UINTN mapSize = 0, mapKey, descriptorSize;
		UINT32 descriptorVersion;
		e = uefi_call_wrapper(ST->BootServices->GetMemoryMap, 5, &mapSize, NULL, &mapKey, &descriptorSize, NULL);

		EFI_MEMORY_DESCRIPTOR * efi_memory = (void*)(final_offset);
		final_offset += mapSize;
		while ((uintptr_t)final_offset & 0x3ff) final_offset++;

		e = uefi_call_wrapper(ST->BootServices->GetMemoryMap, 5, &mapSize, efi_memory, &mapKey, &descriptorSize, NULL);

		if (EFI_ERROR(e)) {
			print_("EFI error.\n");
			while (1) {};
		}

		uint64_t upper_mem = 0;
		int descriptors = mapSize / descriptorSize;
		for (int i = 0; i < descriptors; ++i) {
			EFI_MEMORY_DESCRIPTOR * d = efi_memory;

			mmap->size = sizeof(uint64_t) * 2 + sizeof(uint32_t);
			mmap->base_addr = d->PhysicalStart;
			mmap->length = d->NumberOfPages * 4096;
			switch (d->Type) {
				case EfiConventionalMemory:
				case EfiLoaderCode:
				case EfiLoaderData:
				case EfiBootServicesCode:
				case EfiBootServicesData:
				case EfiRuntimeServicesCode:
				case EfiRuntimeServicesData:
					mmap->type = 1;
					break;
				case EfiReservedMemoryType:
				case EfiUnusableMemory:
				case EfiMemoryMappedIO:
				case EfiMemoryMappedIOPortSpace:
				case EfiPalCode:
				case EfiACPIMemoryNVS:
				case EfiACPIReclaimMemory:
				default:
					mmap->type = 2;
					break;
			}
			if (mmap->type == 1 && mmap->base_addr >= 0x100000) {
				upper_mem += mmap->length;
			}
			mmap = (mboot_memmap_t *) ((uintptr_t)mmap + mmap->size + sizeof(uint32_t));
			efi_memory = (EFI_MEMORY_DESCRIPTOR *)((char *)efi_memory + descriptorSize);
		}

		finalHeader->mmap_length = (uintptr_t)mmap - finalHeader->mmap_addr;

		finalHeader->mem_lower = 1024;
		finalHeader->mem_upper = upper_mem / 1024;
	}

	relocate_ramdisk((void*)(uintptr_t)finalHeader->mods_addr);

	{
		EFI_STATUS e;
		UINTN mapSize = 0, mapKey, descriptorSize;
		UINT32 descriptorVersion;
		uefi_call_wrapper(ST->BootServices->GetMemoryMap, 5, &mapSize, NULL, &mapKey, &descriptorSize, NULL);
		e = uefi_call_wrapper(ST->BootServices->ExitBootServices, 2, ImageHandleIn, mapKey);

		if (e != EFI_SUCCESS) { 
			print_("Exit services failed. \n");
			print_hex_(e);
			while (1) {};
		}
	}

	/* Jump to entry with register arguments */
	__asm__ __volatile__ (
		"jmp %0" :: "r"(_xmain), "a"(MULTIBOOT_EAX_MAGIC), "b"(finalHeader));

	__builtin_unreachable();
}

void boot(void) {
	UINTN count;
	EFI_HANDLE * handles;
	EFI_LOADED_IMAGE * loaded_image;
	EFI_FILE_IO_INTERFACE *efi_simple_filesystem;
	EFI_FILE *root;
	EFI_STATUS status;

	uefi_call_wrapper(ST->BootServices->SetWatchdogTimer, 4, 0, 0, 0, NULL);

	clear_();

	draw_logo(0);

	for (unsigned int i = 0; i < ST->NumberOfTableEntries; ++i) {
		/* ACPI 1 table pointer. */
		if (ST->ConfigurationTable[i].VendorGuid.Data1 == 0xeb9d2d30 &&
			ST->ConfigurationTable[i].VendorGuid.Data2 == 0x2d88 &&
			ST->ConfigurationTable[i].VendorGuid.Data3 == 0x11d3) {
			multiboot_header.config_table = (uintptr_t)ST->ConfigurationTable[i].VendorTable & 0xFFFFffff;
			break;
		}
		/* ACPI 2 table pointer. */
		if (ST->ConfigurationTable[i].VendorGuid.Data1 == 0x8868e871 &&
			ST->ConfigurationTable[i].VendorGuid.Data2 == 0xe4f1 &&
			ST->ConfigurationTable[i].VendorGuid.Data3 == 0x11d3) {
			multiboot_header.config_table = (uintptr_t)ST->ConfigurationTable[i].VendorTable & 0xFFFFffff;
			break;
		}
	}

	status = uefi_call_wrapper(ST->BootServices->HandleProtocol,
			3, ImageHandleIn, &efi_loaded_image_protocol_guid,
			(void **)&loaded_image);

	if (EFI_ERROR(status)) {
		print_("Could not obtain loaded_image_protocol\n");
		while (1) {};
	}

	print("Found loaded image...\n");

	status = uefi_call_wrapper(ST->BootServices->HandleProtocol,
			3, loaded_image->DeviceHandle, &efi_simple_file_system_protocol_guid,
			(void **)&efi_simple_filesystem);

	if (EFI_ERROR(status)) {
		print_("Could not obtain simple_file_system_protocol.\n");
		while (1) {};
	}

	status = uefi_call_wrapper(efi_simple_filesystem->OpenVolume,
			2, efi_simple_filesystem, &root);

	if (EFI_ERROR(status)) {
		print_("Could not open volume.\n");
		while (1) {};
	}

	EFI_FILE * file;

	CHAR16 kernel_name[16] = {0};
	{
		char * c = kernel_path;
		char * ascii = c;
		int i = 0;
		while (*ascii) {
			kernel_name[i] = *ascii;
			i++;
			ascii++;
		}
		if (kernel_name[i-1] == L'.') {
			kernel_name[i-1] = 0;
		}
	}

	/* Load kernel */
	status = uefi_call_wrapper(root->Open,
			5, root, &file, kernel_name, EFI_FILE_MODE_READ, 0);

	if (EFI_ERROR(status)) {
		print_("Error opening kernel.\n");
		while (1) {};
	}

#define KERNEL_LOAD_START 0x4000000ULL
	kernel_load_start = (char*)KERNEL_LOAD_START;

	{
		EFI_PHYSICAL_ADDRESS addr = KERNEL_LOAD_START;
		EFI_ALLOCATE_TYPE type = AllocateAddress;
		EFI_MEMORY_TYPE memtype = EfiLoaderData;
		UINTN pages = 8192;
		EFI_STATUS status = 0;
		status = uefi_call_wrapper(ST->BootServices->AllocatePages, 4, type, memtype, pages, &addr);
		if (EFI_ERROR(status)) {
			print_("Could not allocate space to load boot payloads: ");
			print_hex_(status);
			print_(" ");
			print_hex_(addr);
			while (1) {};
		}
	}

	unsigned int offset = 0;
	UINTN bytes = 134217728;
	status = uefi_call_wrapper(file->Read,
			3, file, &bytes, (void *)KERNEL_LOAD_START);

	if (EFI_ERROR(status)) {
		print_("Error loading kernel.\n");
		while (1) {};
	}

	offset += bytes;
	while (offset % 4096) offset++;

	{
		char * c = ramdisk_path;
		CHAR16 name[16] = {0};
		char * ascii = c;
		int i = 0;
		while (*ascii) {
			name[i] = *ascii;
			i++;
			ascii++;
		}
		if (name[i-1] == L'.') {
			name[i-1] == 0;
		}
		bytes = 134217728;
		status = uefi_call_wrapper(root->Open,
				5, root, &file, name, EFI_FILE_MODE_READ, 0);
		if (!EFI_ERROR(status)) {
			status = uefi_call_wrapper(file->Read,
					3, file, &bytes, (void*)(uintptr_t)(KERNEL_LOAD_START + offset));
			if (!EFI_ERROR(status)) {
				ramdisk_off = KERNEL_LOAD_START + offset;
				ramdisk_len = bytes;
			} else {
				print_("Failed to read ramdisk\n");
			}
		} else {
			print_("Error opening "); print_(c); print_("\n");
		}
	}

	load_kernel();
	finish_boot();
}

#else
struct mmap_entry {
	uint64_t base;
	uint64_t len;
	uint32_t type;
	uint32_t reserved;
};

extern unsigned short mmap_ent;
extern unsigned short lower_mem;

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

	print("Jumping to kernel...\n");

	uint32_t foo[3];
	foo[0] = MULTIBOOT_EAX_MAGIC;
	foo[1] = (unsigned int)&multiboot_header;;
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

extern void do_bios_call(uint32_t function, uint32_t arg1);
extern int bios_call(char * into, uint32_t sector);

static int spin_x = 0;
static void spin(void) {
	static int  spincnt = 0;
	draw_logo(spincnt+1);
	spincnt = (spincnt + 1) & 0x7;
}

static void clear_spin(void) {
	y = 16;
	//print_banner("");
}

extern uint16_t * vbe_cont_info_mode_off;
extern uint32_t *vbe_info_fbaddr;
extern uint16_t vbe_info_pitch;
extern uint16_t vbe_info_width;
extern uint16_t vbe_info_height;
extern uint8_t vbe_info_bpp;

extern void bios_text_mode(void);

void boot(void) {
	/* Did we ask for VGA text mode and are currently in a video mode? */
	if (boot_mode == 5) {
		bios_text_mode();
	}

	clear_();

	draw_logo(0);

	print("Looking for ISO9660 filesystem... ");
	for (int i = 0x10; i < 0x15; ++i) {
		bios_call((char*)(DATA_LOAD_BASE + ISO_SECTOR_SIZE * i), i);
		root = (void*)(DATA_LOAD_BASE + ISO_SECTOR_SIZE * i);
		switch (root->type) {
			case 1:
				print("found.\n");
				goto done;
		}
	}
	return;
done:

	print("Looking for kernel... ");
	if (!navigate(kernel_path)) {
		print_("Failed to locate kernel.\n");
		return;
	}
	print("found.\n");

	kernel_load_start = (char*)(DATA_LOAD_BASE + dir_entry->extent_start_LSB * ISO_SECTOR_SIZE);

	print("Loading kernel... "); spin_x = x;
	for (int i = 0, j = 0; i < dir_entry->extent_length_LSB; j++) {
		if (!(j & 0x3FF)) spin();
		bios_call(kernel_load_start + i, dir_entry->extent_start_LSB + j);
		i += ISO_SECTOR_SIZE;
	}
	print("\n");

	print("Looking for ramdisk... ");
	if (!navigate(ramdisk_path)) {
		print_("Failed to locate ramdisk.\n");
		return;
	}
	print("found.\n");

	ramdisk_off = DATA_LOAD_BASE + dir_entry->extent_start_LSB * ISO_SECTOR_SIZE;

	print("Loading ramdisk... "); spin_x = x;
	for (int i = 0, j = 0; i < dir_entry->extent_length_LSB; j++) {
		if (!(j & 0x3FF)) spin();
		bios_call((char*)(ramdisk_off + i), dir_entry->extent_start_LSB + j);
		i += ISO_SECTOR_SIZE;
	}
	print("\n");

	ramdisk_len = dir_entry->extent_length_LSB;

	multiboot_header.cmdline = (uintptr_t)cmdline;

	draw_logo(0);

	if (vbe_info_width) {
		multiboot_header.framebuffer_addr   = (uint32_t)vbe_info_fbaddr;
		multiboot_header.framebuffer_pitch  = vbe_info_pitch;
		multiboot_header.framebuffer_width  = vbe_info_width;
		multiboot_header.framebuffer_height = vbe_info_height;
		multiboot_header.framebuffer_bpp    = vbe_info_bpp;
	}

	print("Loading kernel from 0x"); print_hex((uint32_t)kernel_load_start); print("... ");
	if (!load_kernel()) {
		print_("Failed to load kernel.\n");
		return;
	}

	print("Relocating ramdisk from 0x"); print_hex((uint32_t)ramdisk_off); print(":0x"); print_hex(ramdisk_len); print(" to 0x"); print_hex(final_offset); print("... ");
	relocate_ramdisk(modules_mboot);

	finish_boot();
}
#endif
