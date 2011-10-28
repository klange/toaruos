/*
 * The ToAru Sample Game
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>

DEFN_SYSCALL1(kbd_mode, 12, int);
DEFN_SYSCALL0(kbd_get, 13);


int main(int argc, char ** argv) {

	syscall_kbd_mode(1);

	int playing = 1;
	while (playing) {

		char ch = 0;
		ch = syscall_kbd_get();
		if (ch) {
			printf("Pressed key %d\n", ch);
		}
	}

	syscall_kbd_mode(0);

	return 0;
}
