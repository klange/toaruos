/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2021 K. Lange
 *
 * splash-log - Display startup messages before UI has started.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

#include <kernel/video.h>
#include <toaru/pex.h>

#include "terminal-font.h"

static FILE * console;

static void update_message(char * c) {
	fprintf(console, "%s\n", c);
}

static FILE * pex_endpoint = NULL;
static void open_socket(void) {
	pex_endpoint = pex_bind("splash");
	if (!pex_endpoint) exit(1);
}

static void say_hello(void) {
	/* Get our release version */
	struct utsname u;
	uname(&u);
	/* Strip git tag */
	char * tmp = strstr(u.release, "-");
	if (tmp) *tmp = '\0';
	/* Setup hello message */
	char hello_msg[512];
	snprintf(hello_msg, 511, "ToaruOS %s is starting up...", u.release);
	/* Add it to the log */
	update_message(hello_msg);
}

int main(int argc, char * argv[]) {
	if (getuid() != 0) {
		fprintf(stderr, "%s: only root should run this\n", argv[0]);
		return 1;
	}

	open_socket();
	console = fopen("/dev/console","a");

	if (!fork()) {
		say_hello();

		while (1) {
			pex_packet_t * p = calloc(PACKET_SIZE, 1);
			pex_listen(pex_endpoint, p);

			if (p->size < 4)  continue; /* Ignore blank messages, erroneous line feeds, etc. */
			if (p->size > 80) continue; /* Ignore overly large messages */

			if (!strncmp((char*)p->data, "!quit", 5)) {
				/* Use the special message !quit to exit. */
				fclose(pex_endpoint);
				return 0;
			} else if (p->data[0] == ':') {
				/* Make sure message is nil terminated (it should be...) */
				char * tmp = malloc(p->size + 1);
				memcpy(tmp, p->data, p->size);
				tmp[p->size] = '\0';
				update_message(tmp+1);
				free(tmp);
			} else {
				/* Make sure message is nil terminated (it should be...) */
				char * tmp = malloc(p->size + 1);
				memcpy(tmp, p->data, p->size);
				tmp[p->size] = '\0';
				update_message(tmp);
				free(tmp);
			}
		}
	}

	return 0;
}
