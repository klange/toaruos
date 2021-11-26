/**
 * @brief Video mode management.
 *
 * Tries to abstract away differences between VESA mode setting
 * on BIOS and GOP mode setting on UEFI. Also provides the
 * video mode selection menu.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2021 K. Lange
 */
#include "text.h"
#include "util.h"
#include "kbd.h"

#ifdef EFI_PLATFORM
#include <efi.h>
extern EFI_SYSTEM_TABLE *ST;
#else
#include <stdint.h>
#endif

int platform_count_modes(int *);
int platform_list_modes(int sel, int select_this_mode);
extern void init_graphics(void);

void mode_selector(int sel, int ndx, char *str) {
	set_attr(sel == ndx ? 0x70 : 0x07);
	print_(str);
	if (x < 26) {
		while (x < 25) {
			print_(" ");
		}
		x = 26;
	} else if (x < 52) {
		while (x < 51) {
			print_(" ");
		}
		x = 52;
	} else {
		print_("\n");
	}
}

static char * print_int_into(char * str, unsigned int value) {
	unsigned int n_width = 1;
	unsigned int i = 9;
	while (value > i && i < UINT32_MAX) {
		n_width += 1;
		i *= 10;
		i += 9;
	}

	char buf[n_width+1];
	for (int i = 0; i < n_width + 1; i++) {
		buf[i] = 0;
	}
	i = n_width;
	while (i > 0) {
		unsigned int n = value / 10;
		int r = value % 10;
		buf[i - 1] = r + '0';
		i--;
		value = n;
	}
	for (char * c = buf; *c; c++) {
		*str++ = *c;
	}
	return str;
}

int video_menu(void) {
	clear_();

	int sel = 0;
	int sel_max = platform_count_modes(&sel);
	int select_this_mode = 0;

	int s = 0;

	do {
		move_cursor(0,0);
		set_attr(0x1f);
		print_banner("Select Video Mode");
		set_attr(0x07);
		print_("\n");

		if (platform_list_modes(sel,select_this_mode)) return 0;

		read_again:
		s = read_scancode(0);
		if (s == 0x50) { /* DOWN */
			if (sel >= 0 && sel < sel_max - 1) {
				sel = (sel + 3) % sel_max;
			} else {
				sel = (sel + 1)  % sel_max;
			}
		} else if (s == 0x48) { /* UP */
			if (sel >= 1) {
				sel = (sel_max + sel - 3)  % sel_max;
			} else {
				sel = (sel_max + sel - 1)  % sel_max;
			}
		} else if (s == 0x4B) { /* LEFT */
			if (sel >= 0) {
				if (sel % 3 != 0) {
					sel = (sel - 1) % sel_max;
				} else {
					sel += 2;
				}
			}
		} else if (s == 0x4D) { /* RIGHT */
			if (sel >= 0) {
				if (sel % 3 != 2) {
					sel = (sel + 1) % sel_max;
				} else {
					sel -= 2;
				}
			}
		} else if (s == 0x1c) {
			select_this_mode = 1;
			continue;
		} else if (s == 0x01) {
			return 0;
		} else {
			goto read_again;
		}
	} while (1);
}

#ifdef EFI_PLATFORM
extern EFI_GRAPHICS_OUTPUT_PROTOCOL * GOP;
int platform_list_modes(int sel, int select_this_mode) {
	int index = 0;
	for (int i = 0; i < GOP->Mode->MaxMode; ++i) {
		EFI_STATUS status;
		UINTN size;
		EFI_GRAPHICS_OUTPUT_MODE_INFORMATION * info;

		status = uefi_call_wrapper(GOP->QueryMode,
				4, GOP, i, &size, &info);

		if (EFI_ERROR(status) || info->PixelFormat != 1) {
			continue;
		}

		if (select_this_mode && sel == index) {
			uefi_call_wrapper(GOP->SetMode, 2, GOP, i);
			init_graphics();
			return 1;
		}

		char tmp[100];
		char * t = tmp;
		t = print_int_into(t, info->HorizontalResolution); *t = 'x'; t++;
		t = print_int_into(t, info->VerticalResolution); *t = '\0';
		mode_selector(sel, index, tmp);
		index++;
	}
	return 0;
}

int platform_count_modes(int * current) {
	int index = 0;
	for (int i = 0; i < GOP->Mode->MaxMode; ++i) {
		EFI_STATUS status;
		UINTN size;
		EFI_GRAPHICS_OUTPUT_MODE_INFORMATION * info;
		status = uefi_call_wrapper(GOP->QueryMode,
				4, GOP, i, &size, &info);
		if (EFI_ERROR(status) || info->PixelFormat != 1) {
			continue;
		} else {
			index++;
		}
	}
	return index;
}
#else
extern void do_bios_call(uint32_t function, uint32_t arg1);

extern uint32_t vbe_cont_info_mode_off;

struct ColorFormat {
	uint8_t mask;
	uint8_t offset;
};

struct VbeMode {
	uint16_t attributes;
	uint16_t old_shit;
	uint16_t granularity;
	uint16_t window_size;
	uint32_t segments;
	uint32_t old_bank_switching_thing;
	uint16_t pitch;
	uint16_t width;
	uint16_t height;
	uint16_t w_y;
	uint8_t  planes;
	uint8_t  bpp;
	uint8_t  banks;
	uint8_t  memory_model;
	uint8_t  bank_size;
	uint8_t  pages;
	uint8_t  reserved;
	struct ColorFormat red;
	struct ColorFormat green;
	struct ColorFormat blue;
	struct ColorFormat alpha;
	uint8_t color_attributes;
	uint32_t framebuffer_addr;
	uint32_t memory_offset;
	uint32_t memory_size;
	uint8_t other[206];
} __attribute__((packed));

extern volatile struct VbeMode vbe_info;
static struct VbeMode vbe_info_save;

static int qualified(void) {
	if (!(vbe_info.attributes & (1 << 7))) return 0;
	if (vbe_info.bpp < 24) return 0;
	if (vbe_info.width < 640) return 0;
	if (vbe_info.height < 480) return 0;
}

static char tmp[40];

int platform_list_modes(int sel, int select_this_mode) {
	uint32_t vbe_addr = ((vbe_cont_info_mode_off & 0xFFFF0000) >> 12) + (vbe_cont_info_mode_off & 0xFFFF);
	memcpy(&vbe_info_save, (char*)&vbe_info, sizeof(struct VbeMode));
	int index = 0;
	for (uint16_t * x = (uint16_t*)vbe_addr; *x != 0xFFFF;  x++) {
		do_bios_call(2, *x);
		if (!qualified()) {
			memcpy((char*)&vbe_info, &vbe_info_save, sizeof(struct VbeMode));
			continue;
		}

		if (select_this_mode && sel == index) {
			extern void bios_set_video(int);
			bios_set_video(*x);
			return 1;
		}

		char * t = tmp;
		t = print_int_into(t, vbe_info.width); *t = 'x'; t++;
		t = print_int_into(t, vbe_info.height); *t = 'x'; t++;
		t = print_int_into(t, vbe_info.bpp); *t = '\0';

		memcpy((char*)&vbe_info, &vbe_info_save, sizeof(struct VbeMode));

		mode_selector(sel, index, tmp);
		index++;
	}

	return 0;
}

extern int last_video_mode;
int platform_count_modes(int * current) {
	uint32_t vbe_addr = ((vbe_cont_info_mode_off & 0xFFFF0000) >> 12) + (vbe_cont_info_mode_off & 0xFFFF);
	int count = 0;
	memcpy(&vbe_info_save, (char*)&vbe_info, sizeof(struct VbeMode));
	for (uint16_t * x = (uint16_t*)vbe_addr; *x != 0xFFFF;  x++) {
		if (*x == last_video_mode) *current = count;
		do_bios_call(2, *x);
		if (!qualified()) continue;
		count++;
	}
	memcpy((char*)&vbe_info, &vbe_info_save, sizeof(struct VbeMode));
	return count;
}

#endif
