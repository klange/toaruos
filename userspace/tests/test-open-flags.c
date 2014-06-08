/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdio.h>
#include <fcntl.h>
int main(int argc, char * argv) {
	printf("O_RDONLY 0x%x\n", O_RDONLY);
	printf("O_WRONLY 0x%x\n", O_WRONLY);
	printf("O_RDWR   0x%x\n", O_RDWR);
	printf("O_APPEND 0x%x\n", O_APPEND);
	printf("O_CREAT  0x%x\n", O_CREAT);
	printf("O_EXCL   0x%x\n", O_EXCL);
	printf("O_TRUNC  0x%x\n", O_TRUNC);
	return 0;
}
