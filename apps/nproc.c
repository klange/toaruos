/**
 * @brief Print the number of available processors.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdio.h>
#include <syscall.h>

int main(int argc, char * argv[]) {
	printf("%ld\n", syscall_nproc());
	return 0;
}
