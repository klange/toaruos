/**
 * @brief Test tool for libc printf formatters.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2020-2021 K. Lange
 */
#include <stdio.h>

int main(int argc, char * argv[]) {
	printf("%.3d\n", 42);
	printf("%.10d\n", 12345);
	printf("%.1d\n", 0);
	printf("%.0d\n", 0);
	printf("%.0d\n", 1);
	printf("%.0d\n", 123);
	return 0;
}
