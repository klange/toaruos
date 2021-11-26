/**
 * @brief Detects if we were booted with QEMU and processes fwcfg.
 *
 * Determines if we're running in QEMU and looks for "fw_cfg" values
 * that can override the boot mode.
 *
 * TODO This should be perfectly capable of passing a full command
 *      line through the fw_cfg interface, but right now we just look
 *      at some bootmode strings...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2021 K. Lange
 */
#include <stdint.h>
#include "util.h"
#include "menu.h"
#include "options.h"
#include "text.h"

struct fw_cfg_file {
	uint32_t size;
	uint16_t select;
	uint16_t reserved;
	char name[56];
};


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

int detect_qemu(void) {
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
		}

		if (bootmode_index != -1) {
			outports(0x510, bootmode_index);
			char tmp[33] = {0};
			for (int i = 0; i < 32 && i < bootmode_size; ++i) {
				tmp[i] = inportb(0x511);
			}
			for (int i = 0; i < base_sel + 1; ++i) {
				if (!strcmp(tmp,boot_mode_names[i].key)) {
					boot_mode = boot_mode_names[i].index;
					return 1;
				}
			}
			print_("fw_cfg boot mode not recognized: ");
			print_(tmp);
			print_("\n");
		}
	}
	return 0;
}

