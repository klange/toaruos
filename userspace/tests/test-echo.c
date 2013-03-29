/*
 * echo-test
 *
 * Prints a test string to standard out
 * that includes various bits of ANSI
 * escape sequences.
 */
#include <stdio.h>

int main(int argc, char ** argv) {

	printf("\n\033[1mBold \033[0m\033[3mItalic \033[1mBold+Italic\033[0m\033[0m \033[4mUnderline\033[0m \033[9mX-Out\033[0m \033[1;3;4;9mEverything\033[0m\n");

	printf("\033[38;2;178;213;238mHello World\033[0m\n");

	for (int i = 0; i < 256; i += 3) {
		printf("\033[48;6;255;0;0;%dmX\033[0m", i);
	}
	printf("\n");

	for (int i = 0; i < 256; i += 3) {
		printf("\033[48;6;255;0;0;0;m\033[38;6;255;0;0;%dmX\033[0m", i);
	}
	printf("\n");

	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
