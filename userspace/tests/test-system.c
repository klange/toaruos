/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdlib.h>
#include <stdio.h>

int main(int arch, char * argv[]) {
	fprintf(stderr, "Calling system(\"echo hello world\")\n");
	int ret = system("echo hello world");
	fprintf(stderr, "Done. Returned %d.\n", ret);
}
