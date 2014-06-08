/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/pex.h"
#include "lib/toastd.h"

int main(int argc, char * argv[]) {
	if (argc < 3) {
		fprintf(stderr, "%s: \"title\" \"message\"\n", argv[0]);
		return 1;
	}

	char * server = getenv("TOASTD");

	if (!server) {
		server = "toastd"; /* Appropriate fallback */
	}

	FILE * c = pex_connect(server);

	if (!c) {
		fprintf(stderr, "%s: Could not connect to toast daemon.\n", argv[0]);
		return 1;
	}

	int size = strlen(argv[1]) + strlen(argv[2]) + 2 + sizeof(notification_t);

	notification_t * note = malloc(size);

	note->ttl = 5; /* seconds */

	memcpy(note->strings, argv[1], strlen(argv[1])+1);
	memcpy(note->strings + strlen(argv[1])+1, argv[2], strlen(argv[2])+1);

	pex_reply(c, size, (void*)note);

	return 0;
}
