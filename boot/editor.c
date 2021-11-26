/**
 * @brief Command line editor.
 *
 * Very rudimentary command line editor so options can be
 * tweaked. Has a couple of nice features like being
 * able to move the cursor. Not intended to be all that
 * robust, and needs to work in EFI and BIOS.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdint.h>
#include "options.h"
#include "text.h"
#include "util.h"
#include "kbd.h"

int boot_edit = 0;

static uint16_t attribute_cache[80*25] = {0};

static void draw_text(int cursor, int len) {
	int i = 0;
	for (int _my_y = 0; _my_y < 25; ++_my_y) {
		for (int _my_x = 0; _my_x < 80; ++_my_x) {
			int ch = (i < len) ? cmdline[i] : ' ';
			int attr = (i == cursor) ? 0x70 : 0x07;
			uint16_t combined = (attr << 8) | (ch & 0xFF);

			if (attribute_cache[i] != combined) {
				move_cursor(_my_x, _my_y);
				set_attr(attr);
				print_((char[]){ch,'\0'});
				attribute_cache[i] = combined;
			}

			i++;
		}
	}
}

int boot_editor(void) {
	int len    = strlen(cmdline);
	int cursor = len;
	int data = 0;

	memset(attribute_cache, 0, sizeof(attribute_cache));

	while (1) {
		draw_text(cursor, len);

		int status;
		do {
			status = read_key(&data);
		} while (status == 1);

		if (status == 0) {
			/* Handle a few special characters */
			if (data == '\n') {
				return 1;
			} else if (data == 27) {
				return 0;
			} else if (data == '\b') {
				if (!cursor) continue;
				if (cursor == len) {
					cmdline[len-1] = '\0';
					cursor--;
					len--;
				} else {
					cmdline[cursor-1] = '\0';
					strcat(cmdline,&cmdline[cursor]);
					cursor--;
					len--;
				}
			} else {
				if (len > 1022) continue;
				/* Move everything from the cursor onward forward */
				if (cursor < len) {
					int x = len + 1;
					while (x > cursor) {
						cmdline[x] = cmdline[x-1];
						x--;
					}
				}
				cmdline[cursor] = data;
				len++;
				cursor++;
			}
		} else if (status == 2) {
			/* Left */
			if (cursor) cursor--;
		} else if (status == 3) {
			/* Right */
			if (cursor < len) cursor++;
		} else if (status == 4) {
			/* Shift-left: Word left */
			while (cursor && cmdline[cursor] == ' ') cursor--;
			while (cursor && cmdline[cursor] != ' ') cursor--;
		} else if (status == 5) {
			/* Shift-right: Word right */
			while (cursor < len && cmdline[cursor] == ' ') cursor++;
			while (cursor < len && cmdline[cursor] != ' ') cursor++;
		}
	}
}

