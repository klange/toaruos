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
 * Copyright (C) 2018-2021 K. Lange
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
#include <sys/times.h>
#include <sys/fswait.h>

#include <kernel/video.h>
#include <toaru/pex.h>
#include <toaru/hashmap.h>

#define TIMEOUT_SECS 2

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

static int tokenize(char * str, char * sep, char **buf) {
	char * pch_i;
	char * save_i;
	int    argc = 0;
	pch_i = strtok_r(str,sep,&save_i);
	if (!pch_i) { return 0; }
	while (pch_i != NULL) {
		buf[argc] = (char *)pch_i;
		++argc;
		pch_i = strtok_r(NULL,sep,&save_i);
	}
	buf[argc] = NULL;
	return argc;
}

static hashmap_t * get_cmdline(void) {
	int fd = open("/proc/cmdline", O_RDONLY);
	char * out = malloc(1024);
	size_t r = read(fd, out, 1024);
	out[r] = '\0';
	if (out[r-1] == '\n') {
		out[r-1] = '\0';
	}

	char * arg = strdup(out);
	char * argv[1024];
	int argc = tokenize(arg, " ", argv);

	/* New let's parse the tokens into the arguments list so we can index by key */

	hashmap_t * args = hashmap_create(10);

	for (int i = 0; i < argc; ++i) {
		char * c = strdup(argv[i]);

		char * name;
		char * value;

		name = c;
		value = NULL;
		/* Find the first = and replace it with a null */
		char * v = c;
		while (*v) {
			if (*v == '=') {
				*v = '\0';
				v++;
				value = v;
				goto _break;
			}
			v++;
		}

_break:
		hashmap_set(args, name, value);
	}

	free(arg);
	free(out);

	return args;
}


int main(int argc, char * argv[]) {
	if (getuid() != 0) {
		fprintf(stderr, "%s: only root should run this\n", argv[0]);
		return 1;
	}

	if (!fork()) {
		hashmap_t * cmdline = get_cmdline();

		int quiet = 0;
		char * last_message = NULL;
		clock_t start = times(NULL);

		if (!hashmap_has(cmdline, "debug")) {
			quiet = 1;
		}

		open_socket();
		console = fopen("/dev/console","a");

		if (!quiet) say_hello();

		while (1) {
			int pex_fd[] = {fileno(pex_endpoint)};
			int index = fswait2(1, pex_fd, 100);

			if (index == 0) {
				pex_packet_t * p = calloc(PACKET_SIZE, 1);
				pex_listen(pex_endpoint, p);

				if (p->size < 4)  { free(p); continue; } /* Ignore blank messages, erroneous line feeds, etc. */
				if (p->size > 80) { free(p); continue; } /* Ignore overly large messages */

				if (!strncmp((char*)p->data, "!quit", 5)) {
					/* Use the special message !quit to exit. */
					fclose(pex_endpoint);
					return 0;
				}

				if (!quiet) {
					p->data[p->size] = '\0';
					update_message((char*)p->data + (p->data[0] == ':' ? 1 : 0));
				}

				if (last_message) {
					free(last_message);
					last_message = NULL;
				}

				if (quiet) {
					last_message = strdup((char*)p->data + (p->data[0] == ':' ? 1 : 0));
				}

				free(p);
			} else if (quiet && times(NULL) - start > TIMEOUT_SECS * 1000000L) {
				quiet = 0;
				if (last_message) {
					update_message("Startup is taking a while, enabling log. Last message was:");
					update_message(last_message);
					free(last_message);
					last_message = NULL;
				} else {
					update_message("Startup is taking a while, enabling log.");
				}
			}
		}
	}

	return 0;
}
