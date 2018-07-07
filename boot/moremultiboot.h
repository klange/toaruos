
static mboot_mod_t modules_mboot[sizeof(modules)/sizeof(*modules)] = {
	{0,0,0,1}
};

static struct multiboot multiboot_header = {
	/* flags;             */ MULTIBOOT_FLAG_CMDLINE | MULTIBOOT_FLAG_MODS | MULTIBOOT_FLAG_MEM | MULTIBOOT_FLAG_MMAP,
	/* mem_lower;         */ 0x100000,
	/* mem_upper;         */ 0x640000,
	/* boot_device;       */ 0,
	/* cmdline;           */ 0,
	/* mods_count;        */ sizeof(modules)/sizeof(*modules),
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

char * final_offset = NULL;

extern char make_it_happen_capn[];

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
#ifdef EFI_PLATFORM
			EFI_PHYSICAL_ADDRESS addr = phdr->p_vaddr;
			uefi_call_wrapper(ST->BootServices->AllocatePages, 3, AllocateAddress, 0x80000000, phdr->p_memsz / 4096 + 1, &addr);
#endif
			memcpy((uint8_t*)phdr->p_vaddr, (uint8_t*)KERNEL_LOAD_START + phdr->p_offset, phdr->p_filesz);
			long r = phdr->p_filesz;
			while (r < phdr->p_memsz) {
				*(char *)(phdr->p_vaddr + r) = 0;
				r++;
			}
		}
	}

	print("Setting up memory map...\n");
#ifdef EFI_PLATFORM
	mboot_memmap_t * mmap = (void*)KERNEL_LOAD_START;
	multiboot_header.mmap_addr = (uintptr_t)mmap;
	mmap->size = 0;
	mmap->base_addr = 0;
	mmap->length = 0;
	mmap->type = 0;

	multiboot_header.mem_lower = 1024;
	multiboot_header.mem_upper = 1024 * 512;

	memcpy(final_offset, cmdline, 1024);
	multiboot_header.cmdline = (uintptr_t)final_offset;
	final_offset += 1024;
	while ((uintptr_t)final_offset & 0x3ff) final_offset++;

	multiboot_header.mods_addr = (uintptr_t)final_offset;
	memcpy(final_offset, &modules_mboot, sizeof(modules_mboot));
	final_offset += sizeof(modules_mboot);
	while ((uintptr_t)final_offset & 0x3ff) final_offset++;

	memcpy(final_offset, &multiboot_header, sizeof(multiboot_header));
	_ebx = (uintptr_t)final_offset;


	print("Jumping to main, good luck.\n");
#else
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

	_ebx = (unsigned int)&multiboot_header;
#endif

	_eax = MULTIBOOT_EAX_MAGIC;
	_xmain = entry;

#ifdef EFI_PLATFORM
	print_("\nExiting boot services and jumping to ");
	print_hex_(entry);
	print_(" with mboot_mag=");
	print_hex_(_eax);
	print_(" and mboot_ptr=");
	print_hex_(_ebx);
	print_("...\n");

	if (1) {
		EFI_STATUS e;
		UINTN mapSize = 0, mapKey, descriptorSize;
		UINT32 descriptorVersion;
		uefi_call_wrapper(ST->BootServices->GetMemoryMap, 5, &mapSize, NULL, &mapKey, &descriptorSize, NULL);
		e = uefi_call_wrapper(ST->BootServices->ExitBootServices, 2, ImageHandleIn, mapKey);

		if (e != EFI_SUCCESS) { 
			print_("Exit services failed. \n");
			print_hex_(e);
		}
	}
#endif

	__asm__ __volatile__ (
		"mov %1,%%eax \n"
		"mov %2,%%ebx \n"
		"jmp *%0" : : "g"(_xmain), "g"(_eax), "g"(_ebx) : "eax", "ebx"
		);
}

static void do_it(struct ata_device * _device) {
	device = _device;
	if (device->atapi_sector_size != 2048) {
		print_hex(device->atapi_sector_size);
		print("\n - bad sector size\n");
		return;
	}
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

	if (navigate(kernel_path)) {
		print("Found kernel.\n");
		print_hex(dir_entry->extent_start_LSB); print(" ");
		print_hex(dir_entry->extent_length_LSB); print("\n");
		long offset = 0;
		for (int i = dir_entry->extent_start_LSB; i < dir_entry->extent_start_LSB + dir_entry->extent_length_LSB / 2048 + 1; ++i, offset += 2048) {

#ifdef EFI_PLATFORM
			EFI_PHYSICAL_ADDRESS addr = KERNEL_LOAD_START + offset;
			uefi_call_wrapper(ST->BootServices->AllocatePages, 3, AllocateAddress, 0x80000000, 1, &addr);
#endif

			ata_device_read_sector_atapi(device, i, (uint8_t *)KERNEL_LOAD_START + offset);
		}
		restore_root();
		if (navigate(module_dir)) {
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
#ifdef EFI_PLATFORM
						EFI_PHYSICAL_ADDRESS addr = KERNEL_LOAD_START + offset;
						uefi_call_wrapper(ST->BootServices->AllocatePages, 3, AllocateAddress, 0x80000000, 1, &addr);
#endif
						ata_device_read_sector_atapi(device, i, (uint8_t *)KERNEL_LOAD_START + offset);
					}
					j++;
				}
				c++;
				restore_mod();
			}
			print("Done.\n");
			restore_root();
			if (navigate(ramdisk_path)) {
				//clear_();
				ramdisk_off = KERNEL_LOAD_START + offset;
				ramdisk_len = dir_entry->extent_length_LSB;
				modules_mboot[multiboot_header.mods_count-1].mod_start = ramdisk_off;
				modules_mboot[multiboot_header.mods_count-1].mod_end = ramdisk_off + ramdisk_len;

				print_("Loading ramdisk");

#ifdef EFI_PLATFORM
						EFI_PHYSICAL_ADDRESS addr = KERNEL_LOAD_START + offset;
						uefi_call_wrapper(ST->BootServices->AllocatePages, 3, AllocateAddress, 0x80000000, ramdisk_len / 4096 + 1, &addr);
#endif

				int i = dir_entry->extent_start_LSB;
				int sectors = dir_entry->extent_length_LSB / 2048 + 1;
#define SECTORS 65536
				while (sectors >= SECTORS) {
					print_(".");
					ata_device_read_sectors_atapi(device, i, (uint8_t *)KERNEL_LOAD_START + offset, SECTORS);

					sectors -= SECTORS;
					offset += 2048 * SECTORS;
					i += SECTORS;
				}
				if (sectors > 0) {
					print_("!");
					ata_device_read_sectors_atapi(device, i, (uint8_t *)KERNEL_LOAD_START + offset, sectors);
					offset += 2048 * sectors;
				}

				final_offset = (uint8_t *)KERNEL_LOAD_START + offset;
				set_attr(0x07);
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

struct fw_cfg_file {
	uint32_t size;
	uint16_t select;
	uint16_t reserved;
	char name[56];
};

static int boot_mode = 0;

void swap_bytes(void * in, int count) {
	char * bytes = in;
	if (count == 4) {
		uint32_t * t = in;
		*t = (bytes[0] << 24) | (bytes[1] << 12) | (bytes[2] << 8) | bytes[3];
	} else if (count == 2) {
		uint16_t * t = in;
		*t = (bytes[0] << 8) | bytes[1];
	}
}

void show_menu(void) {

#if 1
	/* Try to detect qemu headless boot */
	outports(0x510, 0x0000);
	if (inportb(0x511) == 'Q' &&
		inportb(0x511) == 'E' &&
		inportb(0x511) == 'M' &&
		inportb(0x511) == 'U') {
		uint32_t count = 0;
		uint8_t * bytes = (uint8_t *)&count;
		outports(0x510,0x0019);
		for (int i = 0; i < 4; ++i) {
			bytes[i] = inportb(0x511);
		}
		swap_bytes(&count, 4);
#if 0
		print_("there are ");
		print_hex_(count);
		print_(" entries\n");
#endif

		unsigned int bootmode_size = 0;
		int bootmode_index = -1;
		for (unsigned int i = 0; i < count; ++i) {
			struct fw_cfg_file file;
			uint8_t * tmp = (uint8_t *)&file;
			for (int j = 0; j < sizeof(struct fw_cfg_file); ++j) {
				tmp[j] = inportb(0x511);
			}
			if (!strcmp(file.name,"opt/org.toaruos.bootmode")) {
				swap_bytes(&file.size, 4);
				swap_bytes(&file.select, 2);
				bootmode_size = file.size;
				bootmode_index = file.select;
			}
#if 0
			print_("selector "); print_hex_(file.select); print_(" is "); print_hex_(file.size); print_(" bytes\n");
			print_("and its name is: "); print_(file.name); print_("\n");
#endif
		}

		if (bootmode_index != -1) {
			outports(0x510, bootmode_index);
			char tmp[33] = {0};
			for (int i = 0; i < 32 && i < bootmode_size; ++i) {
				tmp[i] = inportb(0x511);
			}
			for (int i = 0; i < BASE_SEL+1; ++i) {
				if (!strcmp(tmp,boot_mode_names[i].key)) {
					boot_mode = boot_mode_names[i].index;
					return;
				}
			}
			print_("fw_cfg boot mode not recognized: ");
			print_(tmp);
			print_("\n");
		}
	}
#endif


	/* Determine number of options */
	sel_max = 0;
	while (boot_options[sel_max].value) {
		sel_max++;
	}
	sel_max += BASE_SEL + 1;

#ifndef EFI_PLATFORM
	outportb(0x3D4, 14);
	outportb(0x3D5, 0xFF);
	outportb(0x3D4, 15);
	outportb(0x3D5, 0xFF);

	inportb(0x3DA);
	outportb(0x3C0, 0x30);
	char b = inportb(0x3C1);
	b &= ~8;
	outportb(0x3c0, b);
#endif

	clear_();

	do {
		move_cursor(0,0);
		set_attr(0x1f);
		print_banner(VERSION_TEXT);
		set_attr(0x07);
		print_("\n");

		for (int i = 0; i < BASE_SEL+1; ++i) {
			set_attr(sel == i ? 0x70 : 0x07);
			print_(" ");
			char tmp[] = {'0' + (i + 1), '.', ' ', '\0'};
			print_(tmp);
			print_(boot_mode_names[i].title);
			print_("\n");
		}

		// put a gap
		set_attr(0x07);
		print_("\n");

		for (int i = 0; i < sel_max - BASE_SEL - 1; ++i) {
			toggle(BASE_SEL + 1 + i, *boot_options[i].value, boot_options[i].title);
		}

		set_attr(0x07);
		move_cursor(x,17);
		print_("\n");
		print_banner(HELP_TEXT);
		print_("\n");

		if (sel > BASE_SEL) {
			print_banner(boot_options[sel-BASE_SEL-1].description_1);
			print_banner(boot_options[sel-BASE_SEL-1].description_2);
			print_("\n");
		} else {
			print_banner(COPYRIGHT_TEXT);
			print_("\n");
			print_banner(LINK_TEXT);
		}

		int s = read_scancode();
		if (s == 0x50) { /* DOWN */
			if (sel > BASE_SEL && sel < sel_max - 1) {
				sel = (sel + 2) % sel_max;
			} else {
				sel = (sel + 1)  % sel_max;
			}
		} else if (s == 0x48) { /* UP */
			if (sel > BASE_SEL + 1) {
				sel = (sel_max + sel - 2)  % sel_max;
			} else {
				sel = (sel_max + sel - 1)  % sel_max;
			}
		} else if (s == 0x4B) { /* LEFT */
			if (sel > BASE_SEL + 1) {
				sel -= 1;
			}
		} else if (s == 0x4D) { /* RIGHT */
			if (sel > BASE_SEL) {
				sel = (sel + 1) % sel_max;
			}
		} else if (s == 0x1c) {
			if (sel <= BASE_SEL) {
				boot_mode = boot_mode_names[sel].index;
				break;
			} else {
				int index = sel - BASE_SEL - 1;
				*boot_options[index].value = !*boot_options[index].value;
			}
		} else if (s >= 2 && s <= 10) {
			int i = s - 2;
			if (i <= BASE_SEL) {
				boot_mode = boot_mode_names[i].index;
				break;
			}
#if 0
		} else {
			print_hex_(s);
#endif
		}
	} while (1);
}

static void boot(void) {

	clear_();

	multiboot_header.cmdline = (uintptr_t)cmdline;

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
