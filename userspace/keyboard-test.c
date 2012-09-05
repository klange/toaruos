/*
 * keyboard-test
 *
 * Waits for key presses in locked keyboard mode
 * and prints them to the screen.
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>

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
