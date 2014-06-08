/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Sample application which triggers a stack overflow
 * by means of a simple infinitely recursive function.
 */
#include <stdio.h>
#include <syscall.h>

void overflow() {
	int i[1024] = {0xff};
	printf("Stack is at 0x%x\n", &i);
	overflow();
}

int main(int argc, char ** argv) {
	overflow();

	return 0;
}
