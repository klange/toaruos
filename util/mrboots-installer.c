/*
 * Mr Boots Installer
 *
 * Installs Mr. Boots onto a generated disk image.
 * Compile me with your standard C library and for whatever
 * architecture you feel like running me on, though I much
 * prefer something simple and 32-bit.
 */
#include <stdint.h>
#include <stdio.h>
/* The EXT2 header is smart enough to know to grab us stdint.h rather than types.h... */
#include "../kernel/include/ext2.h"

int main(int argc, char ** argv) {
	if (argc < 3) {
		fprintf(stderr, "Expected two additional arguments: a ramdisk, and a file path to second stage to find in it.\n");
		return -1;
	}
	fprintf(stderr, "I will look for %s in %s and generate appropriate output.\n", argv[2], argv[1]);
}
