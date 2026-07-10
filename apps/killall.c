/**
 * @brief killall - Send signals to processes matching name
 *
 * Find processes by name and send them signals.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/signal.h>

#include <toaru/procfs.h>

int show_usage(int argc, char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - send a signal to all process with the given names\n"
			"\n"
			"usage: %s [-" X_S "name" X_E "] " X_S "procname..." X_E "\n"
			"\n"
			" --help          " X_S "Show this help message." X_E "\n"
			" -s " X_S "name         Specify the signal to send." X_E "\n"
			"\n"
			"Signals may be named with or without the SIG prefix, or numerically.\n",
			argv[0], argv[0]);
	return 1;
}

struct KillallContext {
	char ** argv;
	int i;
	int signum;
	int killed_something;
};

static int killall_callback(struct process * proc, void * ctx) {
	struct KillallContext * this = ctx;

	if (!strcmp(proc->name, this->argv[this->i])) {
		if (kill(proc->pid, this->signum) < 0) {
			fprintf(stderr, "%s: %s(%d): %s\n", this->argv[0], this->argv[this->i], proc->pid, strerror(errno));
		} else {
			this->killed_something = 1;
		}
	}

	return 0;
}

int main (int argc, char * argv[]) {

	struct KillallContext ctx = {argv, -1, SIGTERM, 0};

	int optind = 1;
	for (; optind < argc; ++optind) {
		if (!strcmp(argv[optind],"--")) {
			optind++;
			break;
		}

		if (*argv[optind] != '-') break;

		char * optarg = &argv[optind][1]; /* -SIGNAL */
		if (*optarg == '-') {
			optarg++;
			if (!strcmp(optarg,"help")) return show_usage(argc, argv), 0;
			else if (!strcmp(optarg,"signal")) optarg = argv[++optind]; /* --signal SIGNAL */
			else if (strstr(optarg,"signal=") == optarg) optarg += 7; /* --signal=SIGNAL */
			else return fprintf(stderr, "%s: --%s: unrecognized option\n", argv[0], optarg), 1;
		} else if (*optarg == 's') { /* -sSIGNAL */
			optarg++;
			if (!*optarg) optarg = argv[++optind]; /* -s SIGNAL */
			if (!optarg) return fprintf(stderr, "%s: -s: missing argument\n", argv[0]), 1;
		}

		for (char *c = optarg; *c; c++) *c = toupper(*c);
		if (str2sig(optarg, &ctx.signum)) return fprintf(stderr,"%s: %s: invalid signal specification\n",argv[0],optarg), 1;
	}

	if (optind >= argc) {
		show_usage(argc, argv);
		return 1;
	}

	int retval = 0;

	for (int i = optind; i < argc; ++i) {
		ctx.killed_something = 0;
		ctx.i = i;
		procfs_iterate(killall_callback, &ctx, PROCFSLIB_NO_THREADS);

		if (!ctx.killed_something) {
			fprintf(stderr, "%s: %s: no process found\n", argv[0], argv[i]);
			retval = 1;
		}
	}

	return retval;
}
