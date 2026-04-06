/**
 * @brief Quick spot check of ftruncate
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char * argv[]) {

	int fd = open("test.file", O_RDWR | O_CREAT);

	ftruncate(fd, 7000);

	lseek(fd, 0, SEEK_SET);

	unsigned char buf[10000];

	ssize_t resp = read(fd, buf, 8000);

	if (resp != 7000) {
		fprintf(stderr, "expected resp to be 7000, was %zd\n", resp);
		return 1;
	}

	/* Assert that 7000 bytes were empty */
	for (size_t i = 0; i < 7000; ++i) {
		if (buf[i] != 0) {
			fprintf(stderr, "Byte %zu was not 0 (%#x)\n", i, (unsigned int)buf[i]);
			return 1;
		}
	}

	/* Write into the whole thing */
	memset(buf, 0xAA, 7000);
	pwrite(fd, buf, 7000, 0);

	/* Assert we can read back out */
	resp = pread(fd, buf, 8000, 0);

	if (resp != 7000) {
		fprintf(stderr, "pread: expected resp to be 7000, was %zd\n", resp);
		return 1;
	}

	for (size_t i = 0; i < 7000; ++i) {
		if (buf[i] != 0xaa) {
			fprintf(stderr, "Byte %zu was not 0xaa (%#x)\n", i, (unsigned int)buf[i]);
			return 1;
		}
	}

	ftruncate(fd, 2000);

	resp = pread(fd, buf, 8000, 0);

	if (resp != 2000) {
		fprintf(stderr, "pread: expected resp to be 2000, was %zd\n", resp);
		return 1;
	}

	for (size_t i = 0; i < 2000; ++i) {
		if (buf[i] != 0xaa) {
			fprintf(stderr, "Byte %zu was not 0xaa (%#x)\n", i, (unsigned int)buf[i]);
			return 1;
		}
	}

	ftruncate(fd, 6000);

	resp = pread(fd, buf, 8000, 0);
	if (resp != 6000) {
		fprintf(stderr, "pread: expected resp to be 6000, was %zd\n", resp);
		return 1;
	}

	for (size_t i = 0; i < 2000; ++i) {
		if (buf[i] != 0xaa) {
			fprintf(stderr, "Byte %zu was not 0xaa (%#x)\n", i, (unsigned int)buf[i]);
			return 1;
		}
	}

	for (size_t i = 2000; i < 6000; ++i) {
		if (buf[i] != 0) {
			fprintf(stderr, "Byte %zu was not 0 (%#x)\n", i, (unsigned int)buf[i]);
			return 1;
		}
	}

	return 0;

}
