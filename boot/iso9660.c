#include <stddef.h>
#include "iso9660.h"
#include "util.h"
#include "text.h"

iso_9660_volume_descriptor_t * root = NULL;
iso_9660_directory_entry_t * dir_entry = NULL;
static char * dir_entries = NULL;

int navigate(char * name) {
	dir_entry = (iso_9660_directory_entry_t*)&root->root;
	dir_entries = (char*)(DATA_LOAD_BASE + dir_entry->extent_start_LSB * ISO_SECTOR_SIZE);
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
#if 1
			print("Found a file: ");
			print(" Name: ");
			print(file_name); print("\n");
#endif
			if (!strcmp(file_name, name)) {
				dir_entry = dir;
				return 1;
			}
		}
		offset += dir->length;
try_again:
		if ((long)(offset) > dir_entry->extent_length_LSB) break;
	}

	return 0;
}


