/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef SYSTEM_H
#define SYSTEM_H

#include <system.h>

#define MULTIBOOT_MAGIC        0x1BADB002
#define MULTIBOOT_EAX_MAGIC    0x2BADB002
#define MULTIBOOT_FLAG_MEM     0x001
#define MULTIBOOT_FLAG_DEVICE  0x002
#define MULTIBOOT_FLAG_CMDLINE 0x004
#define MULTIBOOT_FLAG_MODS    0x008
#define MULTIBOOT_FLAG_AOUT    0x010
#define MULTIBOOT_FLAG_ELF     0x020
#define MULTIBOOT_FLAG_MMAP    0x040
#define MULTIBOOT_FLAG_CONFIG  0x080
#define MULTIBOOT_FLAG_LOADER  0x100
#define MULTIBOOT_FLAG_APM     0x200
#define MULTIBOOT_FLAG_VBE     0x400

struct multiboot
{
	uintptr_t flags;
	uintptr_t mem_lower;
	uintptr_t mem_upper;
	uintptr_t boot_device;
	uintptr_t cmdline;
	uintptr_t mods_count;
	uintptr_t mods_addr;
	uintptr_t num;
	uintptr_t size;
	uintptr_t addr;
	uintptr_t shndx;
	uintptr_t mmap_length;
	uintptr_t mmap_addr;
	uintptr_t drives_length;
	uintptr_t drives_addr;
	uintptr_t config_table;
	uintptr_t boot_loader_name;
	uintptr_t apm_table;
	uintptr_t vbe_control_info;
	uintptr_t vbe_mode_info;
	uintptr_t vbe_mode;
	uintptr_t vbe_interface_seg;
	uintptr_t vbe_interface_off;
	uintptr_t vbe_interface_len;
} __attribute__ ((packed));

typedef struct {
	uint16_t attributes;
	uint8_t  winA, winB;
	uint16_t granularity;
	uint16_t winsize;
	uint16_t segmentA, segmentB;
	uint32_t realFctPtr;
	uint16_t pitch;

	uint16_t Xres, Yres;
	uint8_t  Wchar, Ychar, planes, bpp, banks;
	uint8_t  memory_model, bank_size, image_pages;
	uint8_t  reserved0;

	uint8_t  red_mask, red_position;
	uint8_t  green_mask, green_position;
	uint8_t  blue_mask, blue_position;
	uint8_t  rsv_mask, rsv_position;
	uint8_t  directcolor_attributes;

	uint32_t physbase;
	uint32_t reserved1;
	uint16_t reserved2;
} __attribute__ ((packed)) vbe_info_t;

typedef struct {
	uintptr_t mod_start;
	uintptr_t mod_end;
	uintptr_t cmdline;
	uintptr_t reserved;
} __attribute__ ((packed)) mboot_mod_t;

typedef struct {
	uint32_t size;
	uint64_t base_addr;
	uint64_t length;
	uint32_t type;
} __attribute__ ((packed)) mboot_memmap_t;

extern struct multiboot *copy_multiboot(struct multiboot *mboot_ptr);
extern void dump_multiboot(struct multiboot *mboot_ptr);
extern char * ramdisk;
extern struct multiboot * mboot_ptr;

#endif
