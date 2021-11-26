/**
 * @brief Show block device statistics, where available.
 *
 * Shows cache hit/miss/write counts for ATA devices, mostly.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>

int main(int argc, char * argv[]) {
	if (argc < 2) return 1;

	int fd = open(argv[1],O_RDONLY);

	if (fd < 0) {
		fprintf(stderr, "open: %d\n", fd);
		return 2;
	}

	uint64_t stats[4] = {-1};

	long res = ioctl(fd, 0x2A01234UL, &stats);

	if (res < 0) {
		fprintf(stderr, "ioctl: %ld\n", res);
		return 3;
	}

	fprintf(stderr, "hits:\t%zu\n", stats[0]);
	fprintf(stderr, "misses:\t%zu\n", stats[1]);
	fprintf(stderr, "evicts:\t%zu\n", stats[2]);
	fprintf(stderr, "writes:\t%zu\n", stats[3]);

	return 0;
}
