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
#include <dirent.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/signal.h>

typedef struct process {
	int pid;
	int ppid;
	int tgid;
	char *name;
	char *path;
} p_t;

#define PROCFSLIB_NO_FREE      1
#define PROCFSLIB_NO_THREADS   2

static p_t * build_entry(struct dirent * dent, int flags) {
	char *fname;
	FILE * f;
	char *line = NULL;
	size_t avail = NULL;
	ssize_t len = 0;

	asprintf(&fname, "/proc/%s/status", dent->d_name);
	f = fopen(fname, "r");
	free(fname);

	p_t * proc = calloc(sizeof(p_t),1);

	while ((len = getline(&line, &avail, f)) != -1) {
		if (len && line[len-1] == '\n') line[len-1] = '\0';
		char * tab = strstr(line,"\t");
		if (tab) {
			*tab = '\0';
			tab++;
		}
		if (strstr(line, "Pid:") == line) {
			proc->pid = atoi(tab);
		} else if (strstr(line, "PPid:") == line) {
			proc->ppid = atoi(tab);
		} else if (strstr(line, "Tgid:") == line) {
			proc->tgid = atoi(tab);
		} else if (strstr(line, "Name:") == line) {
			proc->name = strdup(tab);
		} else if (strstr(line, "Path:") == line) {
			proc->path = strdup(tab);
		}
	}

	if (!proc->name) proc->name = strdup("");
	if (!proc->path) proc->path = strdup("");

	if (proc->tgid != proc->pid) {
		char * tmp;
		asprintf(&tmp, "{%s}", proc->name);
		free(proc->name);
		proc->name = tmp;
	}

	fclose(f);
	if (line) free(line);

	return proc;
}

void procfs_free(struct process * proc) {
	free(proc->name);
	free(proc->path);
	free(proc);
}

int procfs_iterate(int (*callback)(struct process *,void*), void *ctx, int flags) {
	int ret = 0;
	DIR * dirp = opendir("/proc");

	for (struct dirent * ent = readdir(dirp); ent; ent = readdir(dirp)) {
		if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
			p_t * proc = build_entry(ent, flags);
			if ((flags & PROCFSLIB_NO_THREADS) && proc->pid != proc->tgid) {
				procfs_free(proc);
				continue;
			}
			if (callback(proc,ctx)) ret = 1;
			if (!(flags & PROCFSLIB_NO_FREE)) procfs_free(proc);
		}
		if (ret) break;
	}
	closedir(dirp);
	return ret;
}

#ifndef PROCFS_LIB_ONLY
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

	int c;
	while ((c = getopt(argc, argv, "s:-:")) != -1) {
		switch (c) {
			case 's':
				for (char *s = optarg; *s; s++) {
					*s = toupper(*s);
				}
				if (str2sig(optarg, &ctx.signum)) {
					fprintf(stderr,"%s: %s: invalid signal specification\n",argv[0],optarg);
					return 1;
				}
				break;
			case '-':
				if (!strcmp(optarg, "help")) {
					show_usage(argc, argv);
					return 0;
				}
				fprintf(stderr, "%s: '--%s' is not a recognized long option.\n", argv[0],optarg);
				/* fall through */
			case '?':
				return show_usage(argc, argv);
		}
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
#endif
