/**
 * @brief Perform DNS lookups.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2018 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

int main(int argc, char * argv[]) {
	if (argc < 2) return 1;

	struct hostent * host = gethostbyname(argv[1]);

	if (!host) {
		fprintf(stderr, "%s: not found\n", argv[1]);
		return 1;
	}

	char * addr = inet_ntoa(*(struct in_addr*)host->h_addr_list[0]);

	fprintf(stderr, "%s: %s\n", host->h_name, addr);
	return 0;
}
