/**
 * @brief Console log manager.
 *
 * Presents a PEX endpoint for startup processes to write log
 * messages to and will only send them to the console if the
 * debug flag is set or 2 seconds have elapsed since we started.
 *
 * This also makes message writes a bit more asynchonrous, which
 * is useful because the framebuffer console output can be quite
 * slow and we don't want to slow down start up processes...
 *
 * The downside to that is that splash-log may have to play catch-up
 * and could still be spewing messages to the console after startup
 * has finished...
 *
 * This used to do a lot more work, as it managed both graphical
 * output of messages and output to the VGA terminal, but that has
 * all moved back into the kernel's 'fbterm', so now we're just
 * a message buffer.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2026 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/times.h>
#include <sys/fswait.h>

#include <toaru/pex.h>
#include <toaru/hashmap.h>

#define TIMEOUT_SECS 2

static void say_hello(void) {
	/* Get our release version */
	struct utsname u;
	uname(&u);
	/* Strip git tag */
	char * tmp = strstr(u.release, "-");
	if (tmp) *tmp = '\0';

	printf("ToaruOS %s is starting up...\n", u.release);
}

#include "../kernel/misc/args.c"
static hashmap_t * get_cmdline(void) {
	char * results = args_from_procfs();
	if (results) free(results);
	return kernel_args_map;
}

static void handle_message(pex_packet_t *p) {
	p->data[p->size] = '\0';
	char *msg = (char*)p->data + (p->data[0] == ':' ? 1 : 0);
	printf("%s\n", msg);
	free(p);
}

int main(int argc, char * argv[]) {
	if (getuid() != 0) {
		fprintf(stderr, "%s: only root should run this\n", argv[0]);
		return 1;
	}

	if (!fork()) {
		hashmap_t * cmdline = get_cmdline();

		int quiet = !hashmap_has(cmdline, "debug");
		pex_packet_t *last_message = NULL;
		clock_t start = times(NULL);

		FILE * pex_endpoint = pex_bind("splash");
		if (!pex_endpoint) return fprintf(stderr, "%s: %s: %s\n", argv[0], "pex", strerror(errno)), 1;

		int console = open("/dev/console",O_APPEND|O_WRONLY);
		if (console == -1) return fprintf(stderr, "%s: %s: %s\n", argv[0], "/dev/console", strerror(errno)), 1;

		dup2(console, STDOUT_FILENO);
		close(console);

		if (!quiet) say_hello();

		while (1) {
			int pex_fd[] = {fileno(pex_endpoint)};
			int index = fswait2(1, pex_fd, 100);

			if (index == 0) {
				pex_packet_t * p = calloc(1, PACKET_SIZE);
				pex_listen(pex_endpoint, p);

				if (p->size < 4)  { free(p); continue; } /* Ignore blank messages, erroneous line feeds, etc. */
				if (p->size > 80) { free(p); continue; } /* Ignore overly large messages */

				if (!strncmp((char*)p->data, "!quit", 5)) {
					/* Use the special message !quit to exit. */
					fclose(pex_endpoint);
					return 0;
				}

				if (quiet) {
					free(last_message);
					last_message = p;
				} else {
					handle_message(p);
				}
			} else if (quiet && times(NULL) - start > TIMEOUT_SECS * 1000000L) {
				quiet = 0;
				printf("Startup is taking a while, enabling log.%s\n", last_message ? " Last message was:" :"");
				if (last_message) {
					handle_message(last_message);
					last_message = NULL;
				}
			}
		}
	}

	return 0;
}
