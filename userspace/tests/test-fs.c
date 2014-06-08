/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/* vim:tabstop=4 shiftwidth=4 noexpandtab
 *
 * File System Test Suite
 * (incomplete)
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

int main(int argc, char ** argv) {
	printf("= Begin File System Testing =\n");

	int fd = creat("/test.log", S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	printf("File descriptor generator: %d\n", fd);

	return 0;
}
