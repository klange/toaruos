#include <stdint.h>
#include "options.h"
#include "text.h"
#include "util.h"

extern int read_key(char * c);

void boot_editor(void) {

	int len    = strlen(cmdline);
	int cursor = len;

	while (1) {
		move_cursor(0,0);
		for (int i = 0; i <= len; ++i) {
			set_attr(i == cursor ? 0x70 : 0x07);
			print_((char[]){cmdline[i],'\0'});
		}
		print_(" ");
		set_attr(0x07);
		do {
			do {
				print_(" ");
			} while (x);
		} while (y);

		char data = 0;
		int status = read_key(&data);

		if (status == 0) {
			/* Handle a few special characters */
			if (data == '\n') {
				return;
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
			if (cursor < len) cursor++;
		}
	}
}

