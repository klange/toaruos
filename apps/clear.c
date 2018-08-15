/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 K. Lange
 *
 * clear - Clear the terminal
 *
 * Sends an escape code to clear the screen. Ideally, this should
 * come from a database of terminal escape codes (eg. terminfo),
 * but we don't have one of those yet, so just send a code that
 * makes sense for a lot of terminals.
 */
#include <stdio.h>

int main(int argc, char ** argv) {
	printf("\033[H\033[2J");
	fflush(stdout);
	return 0;
}
