#include <string.h>
#include "types.h"
#include "iso9660.h"
#include "atapi.h"

iso_9660_volume_descriptor_t * root = (iso_9660_volume_descriptor_t *)((uint8_t *)0x4000000);
iso_9660_directory_entry_t * dir_entry = (iso_9660_directory_entry_t *)((uint8_t *)0x4000800);
uint8_t * mod_dir = (uint8_t *)0x4001000;
uint8_t * dir_entries = (uint8_t *)(0x4010000);
struct ata_device * device = 0;

int navigate(char * name) {
	memset(dir_entries, 2048, 0xA5);
	//print("reading from sector ");
	//print_hex(dir_entry->extent_start_LSB);
	//print("\n");
	ata_device_read_sector_atapi(device, dir_entry->extent_start_LSB, dir_entries);
	ata_device_read_sector_atapi(device, dir_entry->extent_start_LSB+1, dir_entries + 2048);
	ata_device_read_sector_atapi(device, dir_entry->extent_start_LSB+2, dir_entries + 4096);

	long offset = 0;
	while (1) {
		iso_9660_directory_entry_t * dir = (iso_9660_directory_entry_t *)(dir_entries + offset);
		if (dir->length == 0) {
			if (offset < dir_entry->extent_length_LSB) {
				offset += 1; // this->block_size - ((uintptr_t)offset % this->block_size);
				goto try_again;
			}
			break;
		}
		if (!(dir->flags & FLAG_HIDDEN)) {
			char file_name[dir->name_len + 1];
			memcpy(file_name, dir->name, dir->name_len);
			file_name[dir->name_len] = 0;
			char * s = strchr(file_name,';');
			if (s) {
				*s = '\0';
			}
#if 0
			print("Found a file: ");
			print(" Name: ");
			print(file_name); print("\n");
#endif
			if (!strcmp(file_name, name)) {
				memcpy(dir_entry, dir, sizeof(iso_9660_directory_entry_t));
				return 1;
			}
		}
		offset += dir->length;
try_again:
		if ((long)(offset) > dir_entry->extent_length_LSB) break;
	}

	return 0;
}

void restore_root(void) {
	memcpy(dir_entry, (iso_9660_directory_entry_t *)&root->root, sizeof(iso_9660_directory_entry_t));

#if 0
	print("Root restored.");
	print("\n Entry len:  "); print_hex( dir_entry->length);
	print("\n File start: "); print_hex( dir_entry->extent_start_LSB);
	print("\n File len:   "); print_hex( dir_entry->extent_length_LSB);
	print("\n");
#endif
}

void restore_mod(void) {
	memcpy(dir_entry, (iso_9660_directory_entry_t *)mod_dir, sizeof(iso_9660_directory_entry_t));
#if 0
	print("mod restored.");
	print("\n Entry len:  "); print_hex( dir_entry->length);
	print("\n File start: "); print_hex( dir_entry->extent_start_LSB);
	print("\n File len:   "); print_hex( dir_entry->extent_length_LSB);
	print("\n");
#endif
}
