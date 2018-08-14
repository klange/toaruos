/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2018 K. Lange
 *
 * cursor-off - Disables the VGA text mode cursor.
 *
 * This is an old tool that calls a special system call
 * to change the VGA text-mode cursor position. The VGA
 * terminal renders its own cursor in software, so we
 * try to move the hardware cursor off screen so it doesn't
 * interfere with the rest of the terminal and look weird.
 */
#include <syscall.h>

int main(int argc, char * argv[]) {
	int x[] = {0xFF,0xFF};
	return syscall_system_function(13, (char **)x);
}
