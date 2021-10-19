extern int kmain();

#ifdef EFI_PLATFORM
#include <efi.h>
#include <efilib.h>
EFI_HANDLE ImageHandleIn;

extern int init_graphics();

EFI_STATUS
	EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	InitializeLib(ImageHandle, SystemTable);
	ST = SystemTable;
	ImageHandleIn = ImageHandle;

	init_graphics();

	return kmain();
}
#else
#include <stdint.h>
#include <stddef.h>
#include "iso9660.h"
#include "util.h"
#include "text.h"
extern char _bss_start[];
extern char _bss_end[];

void * memcpy(void * restrict dest, const void * restrict src, long n) {
	asm volatile("cld; rep movsb"
	            : "=c"((int){0})
	            : "D"(dest), "S"(src), "c"(n)
	            : "flags", "memory");
	return dest;
}

void * memset(void * dest, int c, long n) {
	asm volatile("cld; rep stosb"
	             : "=c"((int){0})
	             : "D"(dest), "a"(c), "c"(n)
	             : "flags", "memory");
	return dest;
}

int bios_main(void) {
	/* Zero BSS */
	memset(&_bss_start,0,(uintptr_t)&_bss_end-(uintptr_t)&_bss_start);

	/* Hide the cursor */
	outportb(0x3D4, 14);
	outportb(0x3D5, 0xFF);
	outportb(0x3D4, 15);
	outportb(0x3D5, 0xFF);

	/* iirc this disables blink? */
	inportb(0x3DA);
	outportb(0x3C0, 0x30);
	char b = inportb(0x3C1);
	b &= ~8;
	outportb(0x3c0, b);

	return kmain();
}

extern void do_bios_call(void);
extern volatile uint16_t dap_sectors;
extern volatile uint32_t dap_buffer;
extern volatile uint32_t dap_lba_low;
extern volatile uint32_t dap_lba_high;
extern uint8_t disk_space[];

int bios_call(char * into, uint32_t sector) {
	dap_buffer = (uint32_t)disk_space;
	dap_lba_low = sector;
	dap_lba_high = 0;
	dap_sectors = 1;
	do_bios_call();
	memcpy(into, disk_space, 2048);
}

iso_9660_volume_descriptor_t * root = NULL;
iso_9660_directory_entry_t * dir_entry = NULL;
static char * dir_entries = NULL;

int navigate(char * name) {
	dir_entry = (iso_9660_directory_entry_t*)&root->root;

	dir_entries = (char*)(DATA_LOAD_BASE + dir_entry->extent_start_LSB * ISO_SECTOR_SIZE);
	bios_call(dir_entries, dir_entry->extent_start_LSB);
	long offset = 0;
	while (1) {
		iso_9660_directory_entry_t * dir = (iso_9660_directory_entry_t *)(dir_entries + offset);
		if (dir->length == 0) {
			if (offset < dir_entry->extent_length_LSB) {
				offset += 1;
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



#endif
