#include "options.h"
#include "text.h"
#include "util.h"
#include "kbd.h"
#include "qemu.h"

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

	/* Determine number of options */
	sel_max = 0;
	while (boot_options[sel_max].value) {
		sel_max++;
	}
	sel_max += base_sel + 1;

	outportb(0x3D4, 14);
	outportb(0x3D5, 0xFF);
	outportb(0x3D4, 15);
	outportb(0x3D5, 0xFF);

	inportb(0x3DA);
	outportb(0x3C0, 0x30);
	char b = inportb(0x3C1);
	b &= ~8;
	outportb(0x3c0, b);

	clear_();

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
		move_cursor(x,17);
		print_("\n");
		print_banner(HELP_TEXT);
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

		int s = read_scancode();
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
		} else if (s >= 2 && s <= 10) {
			int i = s - 2;
			if (i <= base_sel) {
				boot_mode = boot_mode_names[i].index;
				break;
			}
		}
	} while (1);
}

