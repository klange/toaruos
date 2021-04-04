#include "text.h"
#include "options.h"

struct option boot_options[25] = {{0}}; /* can't really hold more than that */
int sel_max = 0;
int sel = 0;

void toggle(int ndx, int value, char *str) {
	set_attr(sel == ndx ? 0x70 : 0x07);
	if (value) {
		print_(" [X] ");
	} else {
		print_(" [ ] ");
	}
	print_(str);
	set_attr(0x07);
	if (x < 40) {
		while (x < 39) {
			print_(" ");
		}
		x = 40;
	} else {
		print_("\n");
	}
}

int _boot_offset = 0;

