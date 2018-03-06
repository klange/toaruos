#include "types.h"
#include "ata.h"
#include "text.h"
#include "util.h"
#include "atapi_imp.h"
#include "iso9660.h"
#include "elf.h"
#include "multiboot.h"

static void restore_root(void) {
	memcpy(dir_entry, (iso_9660_directory_entry_t *)&root->root, sizeof(iso_9660_directory_entry_t));

#if 1
	print("Root restored.");
	print("\n Entry len:  "); print_hex( dir_entry->length);
	print("\n File start: "); print_hex( dir_entry->extent_start_LSB);
	print("\n File len:   "); print_hex( dir_entry->extent_length_LSB);
	print("\n");
#endif
}

static void restore_mod(void) {
	memcpy(dir_entry, (iso_9660_directory_entry_t *)mod_dir, sizeof(iso_9660_directory_entry_t));
#if 0
	print("mod restored.");
	print("\n Entry len:  "); print_hex( dir_entry->length);
	print("\n File start: "); print_hex( dir_entry->extent_start_LSB);
	print("\n File len:   "); print_hex( dir_entry->extent_length_LSB);
	print("\n");
#endif
}

#define KERNEL_LOAD_START 0x300000

#ifdef _VGA_BOOT
static char * modules[] = {
	"ZERO.KO",
	"RANDOM.KO",
	"NONE",//"SERIAL.KO",
	"NONE",//"DEBUG_SH.KO",
	"PROCFS.KO",
	"TMPFS.KO",
	"NONE",//"ATA.KO",
	"EXT2.KO",
	"NONE",//"ISO9660.KO",
	"PS2KBD.KO",
	"PS2MOUSE.KO",
	"NONE",//"LFBVIDEO.KO",
	"NONE",//"VBOXGUES.KO",
	"NONE",//"VMWARE.KO",
	"NONE",//"VIDSET.KO",
	"PACKETFS.KO",
	"NONE",//"SND.KO",
	"NONE",//"AC97.KO",
	"NONE",//"NET.KO",
	"NONE",//"PCNET.KO",
	"NONE",//"RTL.KO",
	"NONE",//"E1000.KO",
	0
};
#else
static char * modules[] = {
	"ZERO.KO",
	"RANDOM.KO",
	"SERIAL.KO",
	"DEBUG_SH.KO",
	"PROCFS.KO",
	"TMPFS.KO",
	"ATA.KO",
	"EXT2.KO",
	"ISO9660.KO",
	"PS2KBD.KO",
	"PS2MOUSE.KO",
	"LFBVIDEO.KO",
	"VBOXGUES.KO",
	"VMWARE.KO",
	"VIDSET.KO",
	"PACKETFS.KO",
	"SND.KO",
	"AC97.KO",
	"NET.KO",
	"PCNET.KO",
	"RTL.KO",
	"E1000.KO",
	0
};
#endif


static mboot_mod_t modules_mboot[23] = {
	{0,0,0,1}
};

static struct multiboot multiboot_header = {
	/* flags;             */ (1 << 3),
	/* mem_lower;         */ 0x100000,
	/* mem_upper;         */ 0x640000,
	/* boot_device;       */ 0,
#ifdef _VGA_BOOT
	/* cmdline;           */ (uintptr_t)"root=/dev/ram0,nocache start=--vga",
#else
	/* cmdline;           */ (uintptr_t)"vid=auto,1024,768 root=/dev/ram0,nocache start=session",
#endif
	/* mods_count;        */ 23,
	/* mods_addr;         */ (uintptr_t)&modules_mboot,
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

static long ramdisk_off = 1;
static long ramdisk_len = 1;

extern void jump_to_main(void);

int _eax = 1;
int _ebx = 1;
int _xmain = 1;

struct mmap_entry {
	uint64_t base;
	uint64_t len;
	uint32_t type;
	uint32_t reserved;
};

extern unsigned short mmap_ent;
extern unsigned short lower_mem;

static void move_kernel(void) {
	clear();
	print("Relocating kernel...\n");

	Elf32_Header * header = (Elf32_Header *)KERNEL_LOAD_START;

	if (header->e_ident[0] != ELFMAG0 ||
	    header->e_ident[1] != ELFMAG1 ||
	    header->e_ident[2] != ELFMAG2 ||
	    header->e_ident[3] != ELFMAG3) {
		print("Kernel is invalid?\n");
	}

	uintptr_t entry = (uintptr_t)header->e_entry;

	for (uintptr_t x = 0; x < (uint32_t)header->e_phentsize * header->e_phnum; x += header->e_phentsize) {
		Elf32_Phdr * phdr = (Elf32_Phdr *)((uint8_t*)KERNEL_LOAD_START + header->e_phoff + x);
		if (phdr->p_type == PT_LOAD) {
			//read_fs(file, phdr->p_offset, phdr->p_filesz, (uint8_t *)phdr->p_vaddr);
			print("Loading a Phdr... ");
			print_hex(phdr->p_vaddr);
			print(" ");
			print_hex(phdr->p_offset);
			print(" ");
			print_hex(phdr->p_filesz);
			print("\n");
			memcpy((uint8_t*)phdr->p_vaddr, (uint8_t*)KERNEL_LOAD_START + phdr->p_offset, phdr->p_filesz);
			long r = phdr->p_filesz;
			while (r < phdr->p_memsz) {
				*(char *)(phdr->p_vaddr + r) = 0;
				r++;
			}
		}
	}

	print("Setting up memory map...\n");
	print_hex(mmap_ent);
	print("\n");
	memset((void*)KERNEL_LOAD_START, 0x00, 1024);
	mboot_memmap_t * mmap = (void*)KERNEL_LOAD_START;
	multiboot_header.mmap_addr = (uintptr_t)mmap;

	struct mmap_entry * e820 = (void*)0x5000;

	uint64_t upper_mem = 0;
	for (int i = 0; i < mmap_ent; ++i) {
		print("entry "); print_hex(i); print("\n");
		print("base: "); print_hex((uint32_t)e820[i].base); print("\n");
		print("type: "); print_hex(e820[i].type); print("\n");

		mmap->size = sizeof(uint64_t) * 2 + sizeof(uintptr_t);
		mmap->base_addr = e820[i].base;
		mmap->length = e820[i].len;
		mmap->type = e820[i].type;
		if (mmap->type == 1 && mmap->base_addr >= 0x100000) {
			upper_mem += mmap->length;
		}
		mmap = (mboot_memmap_t *) ((uintptr_t)mmap + mmap->size + sizeof(uintptr_t));
	}

	print("lower "); print_hex(lower_mem); print("KB\n");
	multiboot_header.mem_lower = 1024;
	print("upper ");
	print_hex(upper_mem >> 32);
	print_hex(upper_mem);
	print("\n");
	
	multiboot_header.mem_upper = upper_mem / 1024;

	int foo;
	//__asm__ __volatile__("jmp %1" : "=a"(foo) : "a" (MULTIBOOT_EAX_MAGIC), "b"((unsigned int)multiboot_header), "r"((unsigned int)entry));
	_eax = MULTIBOOT_EAX_MAGIC;
	_ebx = (unsigned int)&multiboot_header;
	_xmain = entry;
	jump_to_main();
}

static void do_it(struct ata_device * _device) {
	device = _device;
	if (device->atapi_sector_size != 2048) {
		print_hex(device->atapi_sector_size);
		print("\n - bad sector size\n");
		return;
	}
	print("Locating stage2...\n");
	for (int i = 0x10; i < 0x15; ++i) {
		ata_device_read_sector_atapi(device, i, (uint8_t *)root);
		switch (root->type) {
			case 1:
				root_sector = i;
				goto done;
			case 0xFF:
				return;
		}
	}
	return;
done:
	restore_root();

	if (navigate("KERNEL.")) {
		print("Found kernel.\n");
		print_hex(dir_entry->extent_start_LSB); print(" ");
		print_hex(dir_entry->extent_length_LSB); print("\n");
		long offset = 0;
		for (int i = dir_entry->extent_start_LSB; i < dir_entry->extent_start_LSB + dir_entry->extent_length_LSB / 2048 + 1; ++i, offset += 2048) {
			ata_device_read_sector_atapi(device, i, (uint8_t *)KERNEL_LOAD_START + offset);
		}
		restore_root();
		if (navigate("MOD")) {
			memcpy(mod_dir, dir_entry, sizeof(iso_9660_directory_entry_t));
			print("Scanning modules...\n");
			char ** c = modules;
			int j = 0;
			while (*c) {
				print("load "); print(*c); print("\n");
				if (!navigate(*c)) {
					print("Failed to locate module! [");
					print(*c);
					multiboot_header.mods_count--;
					print("]\n");
				} else {
					modules_mboot[j].mod_start = KERNEL_LOAD_START + offset;
					modules_mboot[j].mod_end = KERNEL_LOAD_START + offset + dir_entry->extent_length_LSB;
					for (int i = dir_entry->extent_start_LSB; i < dir_entry->extent_start_LSB + dir_entry->extent_length_LSB / 2048 + 1; ++i, offset += 2048) {
						ata_device_read_sector_atapi(device, i, (uint8_t *)KERNEL_LOAD_START + offset);
					}
					j++;
				}
				c++;
				restore_mod();
			}
			print("Done.\n");
			restore_root();
			if (navigate("RAMDISK.IMG")) {
				print("Loading ramdisk...\n");
				ramdisk_off = KERNEL_LOAD_START + offset;
				ramdisk_len = dir_entry->extent_length_LSB;
				modules_mboot[multiboot_header.mods_count-1].mod_start = ramdisk_off;
				modules_mboot[multiboot_header.mods_count-1].mod_end = ramdisk_off + ramdisk_len;
				for (int i = dir_entry->extent_start_LSB; i < dir_entry->extent_start_LSB + dir_entry->extent_length_LSB / 2048 + 1; ++i, offset += 2048) {
					if (i % 32 == 0) {
						print(".");
					}
					ata_device_read_sector_atapi(device, i, (uint8_t *)KERNEL_LOAD_START + offset);
				}
				print("Done.\n");
				move_kernel();
			}
		} else {
			print("No mod directory?\n");
		}
	} else {
		print("boo\n");
	}

	return;
}

int kmain() {
	clear();
	print("ToaruOS-NIH Bootloader v0.1\n\n");
	print("Scanning ATA devices.\n");

	ata_device_detect(&ata_primary_master);
	ata_device_detect(&ata_primary_slave);
	ata_device_detect(&ata_secondary_master);
	ata_device_detect(&ata_secondary_slave);

	if (ata_primary_master.is_atapi) {
		do_it(&ata_primary_master);
	}
	if (ata_primary_slave.is_atapi) {
		do_it(&ata_primary_slave);
	}
	if (ata_secondary_master.is_atapi) {
		do_it(&ata_secondary_master);
	}
	if (ata_secondary_slave.is_atapi) {
		do_it(&ata_secondary_slave);
	}


	while (1);
}
