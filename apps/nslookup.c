/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2018 K. Lange
 *
 * nslookup - perform nameserver lookups
 *
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/socket.h>

static void ip_ntoa(uint32_t src_addr, char * out) {
	sprintf(out, "%d.%d.%d.%d",
		(unsigned int)((src_addr & 0xFF000000) >> 24),
		(unsigned int)((src_addr & 0xFF0000) >> 16),
		(unsigned int)((src_addr & 0xFF00) >> 8),
		(unsigned int)((src_addr & 0xFF)));
}

int main(int argc, char * argv[]) {
	if (argc < 2) return 1;

	struct hostent * host = gethostbyname(argv[1]);

	if (!host) {
		fprintf(stderr, "%s: not found\n", argv[1]);
		return 1;
	}

	char addr[16] = {0};
	ip_ntoa(*(uint32_t *)host->h_addr_list[0], addr);

	fprintf(stderr, "%s: %s\n", host->h_name, addr);
	return 0;
}
