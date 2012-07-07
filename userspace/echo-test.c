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

	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
