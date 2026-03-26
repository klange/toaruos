/**
 * @brief Look up the PIDs of all processes with a particular name
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <toaru/procfs.h>

struct PidList {
	int pid;
	struct PidList * next;
};

struct PidofContext {
	char ** argv;
	int i;
	int found_something;
	int single_shot;
	int quiet;
	char * sep;
	struct PidList * omit;
};

int pidof_callback(struct process * proc, void * ctx) {
	struct PidofContext * this= ctx;

	if (this->single_shot && this->found_something) return 1;

	if (!strcmp(proc->name, this->argv[this->i])) {
		struct PidList * omit = this->omit;
		while (omit) {
			if (omit->pid == proc->pid) return 0;
			omit = omit->next;
		}
		if (this->found_something++) {
			if (this->quiet) return 1; /* Quiet implies single-shot */
			printf("%s", this->sep);
		}
		printf("%d", proc->pid);
	}

	return 0;
}

int add_omit(struct PidofContext * ctx, char * arg) {
	int pid = 0;
	if (!strcmp(arg, "%PPID")) {
		pid = getppid();
	} else if (*arg < '0' || *arg > '9') {
		return 1;
	} else {
		pid = atoi(arg);
	}

	struct PidList * this = malloc(sizeof(struct PidList));
	this->pid = pid;
	this->next = ctx->omit;
	ctx->omit = this;
	return 0;
}

int usage(char * argv[]) {

	return 1;
}

int main (int argc, char * argv[]) {

	struct PidofContext ctx = {argv, -1, 0, 0, 0, " ", NULL};
	int opt;

	while ((opt = getopt(argc, argv, "sqd:o:")) != -1) {
		switch (opt) {
			case 'q':
				ctx.quiet = 1;
				/* fallthrough */
			case 's':
				ctx.single_shot = 1;
				break;
			case 'd':
				ctx.sep = optarg;
				break;
			case 'o':
				if (add_omit(&ctx, optarg)) return usage(argv);
				break;
			case '?':
				return usage(argv);
		}
	}

	for (int i = optind; i < argc; ++i) {
		ctx.i = i;
		if (procfs_iterate(pidof_callback, &ctx, PROCFSLIB_NO_THREADS)) break;
	}

	if (!ctx.found_something) return 1;

	if (!ctx.quiet) printf("\n");
	return 0;
}
