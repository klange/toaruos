/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * reset - make the terminal sane, and clear it
 */
#include <stdlib.h>

int main(int argc, char * argv[]) {
	system("stty sane");
	system("clear");
	return 0;
}
