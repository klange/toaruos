/**
 * @brief Present configuration menus.
 *
 * Handles display and user interaction for the config menu.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2021 K. Lange
 */
#include "options.h"
#include "text.h"
#include "util.h"
#include "kbd.h"
#include "qemu.h"
#include "editor.h"

extern void draw_logo(int);

struct option boot_options[20] = {{0}};

int sel_max = 0;
int sel = 0;
int boot_mode = 0;

void toggle(int ndx, int value, char *str) {
	set_attr(sel == ndx ? 0x70 : 0x07);
	if (value) {
		print_(" [X] ");
	} else {
		print_(" [ ] ");
	}
	print_(str);
	if (x < 40) {
		while (x < 39) {
			print_(" ");
		}
		x = 40;
	} else {
		print_("\n");
	}
}


void show_menu(void) {
	if (detect_qemu()) return;

	static int timeout_shown = 0;

	int s;

	int timeout = 4;
	char timeout_msg[] = "Normal boot will commence in 0 seconds.";
	char * timeout_val = strchr(timeout_msg,'0');

	/* Determine number of options */
	sel_max = 0;
	while (boot_options[sel_max].value) {
		sel_max++;
	}
	sel_max += base_sel + 1;
	clear_();

	if (!timeout_shown) {
		timeout_shown = 1;
		draw_logo(10);
		while (1) {
			move_cursor(0,15);
			*timeout_val = timeout + '0';
			set_attr(0x08);
			print_banner("Press <Enter> to boot now, <e> to edit command line,");
			print_banner("or use \030/\031/\032/\033 to select a menu option.");
			print_banner(timeout_msg);

			s = read_scancode(1);
			if (timeout && s == -1) {
				timeout--;
				if (!timeout) {
					boot_mode = boot_mode_names[sel].index;
					return;
				}
				continue;
			}

			clear_();
			goto _key_read;
		}
	}

	do {
		move_cursor(0,0);
		set_attr(0x1f);
		print_banner(VERSION_TEXT);
		set_attr(0x07);
		print_("\n");

		for (int i = 0; i < base_sel + 1; ++i) {
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

		for (int i = 0; i < sel_max - base_sel - 1; ++i) {
			toggle(base_sel + 1 + i, *boot_options[i].value, boot_options[i].title);
		}

		set_attr(0x07);
		move_cursor(0,17);
		print_banner(sel <= base_sel ? HELP_TEXT : HELP_TEXT_OPT);
		print_("\n");

		if (sel > base_sel) {
			print_banner(boot_options[sel - base_sel - 1].description_1);
			print_banner(boot_options[sel - base_sel - 1].description_2);
			print_("\n");
		} else {
			print_banner(COPYRIGHT_TEXT);
			print_("\n");
			print_banner(LINK_TEXT);
		}

		read_again:
		timeout = 0;
		s = read_scancode(0);
		_key_read:
		if (s == 0x50) { /* DOWN */
			if (sel > base_sel && sel < sel_max - 1) {
				sel = (sel + 2) % sel_max;
			} else {
				sel = (sel + 1)  % sel_max;
			}
		} else if (s == 0x48) { /* UP */
			if (sel > base_sel + 1) {
				sel = (sel_max + sel - 2)  % sel_max;
			} else {
				sel = (sel_max + sel - 1)  % sel_max;
			}
		} else if (s == 0x4B) { /* LEFT */
			if (sel > base_sel) {
				if ((sel - base_sel) % 2) {
					sel = (sel + 1) % sel_max;
				} else {
					sel -= 1;
				}
			}
		} else if (s == 0x4D) { /* RIGHT */
			if (sel > base_sel) {
				if ((sel - base_sel) % 2) {
					sel = (sel + 1) % sel_max;
				} else {
					sel -= 1;
				}
			}
		} else if (s == 0x1c) {
			if (sel <= base_sel) {
				boot_mode = boot_mode_names[sel].index;
				break;
			} else {
				int index = sel - base_sel - 1;
				*boot_options[index].value = !*boot_options[index].value;
			}
		} else if (s == 0x12) { /* e */
			if (sel <= base_sel) {
				boot_edit = 1;
				boot_mode = boot_mode_names[sel].index;
				break;
			}
		} else if (s >= 2 && s <= 10) {
			int i = s - 2;
			if (i <= base_sel) {
				boot_mode = boot_mode_names[i].index;
				break;
			}
#ifndef EFI_PLATFORM
		} else if (s == 0x2f) { /* v */
			void bios_toggle_mode(void);
			bios_toggle_mode();
#endif
		} else if (!timeout) {
			goto read_again;
		}
	} while (1);
}

