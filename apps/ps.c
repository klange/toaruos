/**
 * @brief Print a list of running processes.
 *
 * The listed processes are limited to ones owned by the current
 * user and are listed in PID order. Various options allow for
 * threads to be shown separately, extra information to be
 * included in the output, etc.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2021 K. Lange
 */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

#include <toaru/list.h>
#include <toaru/hashmap.h>
#include <toaru/procfs.h>

static int show_all = 0;
static int show_threads = 0;
static int show_username = 0;
static int show_mem = 0;
static int show_cpu = 0;
static int show_time = 0;
static int collect_commandline = 0;

static int widths[] = {3,3,4,3,3,4,4,4};

static hashmap_t * process_ents = NULL;
static list_t * ents_list = NULL;

struct process * process_from_pid(pid_t pid) {
	return hashmap_get(process_ents, (void*)(uintptr_t)pid);
}

int ps_callback(struct process * proc, void * ctx) {
	if (!show_all && proc->uid != getuid()) {
		procfs_free(proc);
		return 0;
	}

	if (!show_threads && proc->tgid != proc->pid) {
		struct process * parent = process_from_pid(proc->tgid);
		if (parent) {
			parent->cpu[0] += proc->cpu[0];
			parent->time =+ proc->time;
		}

		procfs_free(proc);
		return 0;
	}

	hashmap_set(process_ents, (void*)(uintptr_t)proc->pid, proc);
	list_insert(ents_list, (void *)proc);

	char garbage[1024];
	int len;

	if ((len = sprintf(garbage, "%d", proc->tgid)) > widths[0]) widths[0] = len;
	if ((len = sprintf(garbage, "%d", proc->pid)) > widths[1]) widths[1] = len;
	if ((len = sprintf(garbage, "%d", proc->vsz)) > widths[3]) widths[3] = len;
	if ((len = sprintf(garbage, "%d", proc->shm)) > widths[4]) widths[4] = len;
	if ((len = sprintf(garbage, "%d.%01d", proc->mem / 10, proc->mem % 10)) > widths[5]) widths[5] = len;
	if ((len = sprintf(garbage, "%d.%01d", proc->cpu[0] / 10, proc->cpu[0] % 10)) > widths[6]) widths[6] = len;
	if ((len = sprintf(garbage, "%lu:%02lu.%02lu",
		(proc->time / (1000000UL * 60 * 60)),
		(proc->time / (1000000UL * 60)) % 60,
		(proc->time / (1000000UL)) % 60)) > widths[7]) widths[7] = len;

	struct passwd * p = getpwuid(proc->uid);
	if (p) {
		if ((len = strlen(p->pw_name)) > widths[2]) widths[2] = len;
	} else {
		if ((len = sprintf(garbage, "%d", proc->uid)) > widths[2]) widths[2] = len;
	}
	endpwent();

	if (collect_commandline && proc->cmdline) {
		/* Replace \x1e with spaces */
		for (size_t i = 0; i < proc->cmdline_len; ++i) {
			if (proc->cmdline[i] == 30) proc->cmdline[i] = ' ';
		}
	}

	return 0;
}

void print_header(void) {
	if (show_username) {
		printf("%-*s ", widths[2], "USER");
	}
	printf("%*s ", widths[0], "PID");
	if (show_threads) {
		printf("%*s ", widths[1], "TID");
	}
	if (show_cpu) {
		printf("%*s ", widths[6], "%CPU");
	}
	if (show_mem) {
		printf("%*s ", widths[5], "%MEM");
		printf("%*s ", widths[3], "VSZ");
		printf("%*s ", widths[4], "SHM");
	}
	if (show_time) {
		printf("%*s ", widths[7], "TIME");
	}
	printf("CMD\n");
}

void print_entry(struct process * out) {
	if (show_username) {
		struct passwd * p = getpwuid(out->uid);
		if (p) {
			printf("%-*s ", widths[2], p->pw_name);
		} else {
			printf("%-*d ", widths[2], out->uid);
		}
		endpwent();
	}
	printf("%*d ", widths[0], out->tgid);
	if (show_threads) {
		printf("%*d ", widths[1], out->pid);
	}
	if (show_cpu) {
		char tmp[10];
		sprintf(tmp, "%*d.%01d", widths[6]-2, out->cpu[0] / 10, out->cpu[0] % 10);
		printf("%*s ", widths[6], tmp);
	}
	if (show_mem) {
		char tmp[10];
		sprintf(tmp, "%*d.%01d", widths[5]-2, out->mem / 10, out->mem % 10);
		printf("%*s ", widths[5], tmp);
		printf("%*d ", widths[3], out->vsz);
		printf("%*d ", widths[4], out->shm);
	}
	if (show_time) {
		char tmp[30];
		sprintf(tmp, "%lu:%02lu.%02lu",
		(out->time / (1000000UL * 60 * 60)),
		(out->time / (1000000UL * 60)) % 60,
		(out->time / (1000000UL)) % 60);

		printf("%*s ", widths[7], tmp);
	}
	if (out->cmdline) {
		printf("%s\n", out->cmdline);
	} else {
		printf("%s\n", out->name);
	}
}

int show_usage(int argc, char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - list running processes\n"
			"\n"
			"usage: %s [-A] [-T] [" X_S "format" X_E "]\n"
			"\n"
			" -A     " X_S "show other users' processes" X_E "\n"
			" -T     " X_S "show threads" X_E "\n"
			" -?     " X_S "show this help text" X_E "\n"
			"\n"
			" [format] supports some BSD options:\n"
			"\n"
			"  a     " X_S "show full command line" X_E "\n"
			"  u     " X_S "use 'user-oriented' format" X_E "\n"
			"\n", argv[0], argv[0]);
	return 1;
}

int main (int argc, char * argv[]) {

	/* Parse arguments */
	int c;
	while ((c = getopt(argc, argv, "AT?")) != -1) {
		switch (c) {
			case 'A':
				show_all = 1;
				break;
			case 'T':
				show_threads = 1;
				break;
			case '?':
				return show_usage(argc, argv);
		}
	}

	if (optind < argc) {
		char * show = argv[optind];
		while (*show) {
			switch (*show) {
				case 'u':
					show_username = 1;
					show_mem = 1;
					show_cpu = 1;
					show_time = 1;
					// fallthrough
				case 'a':
					collect_commandline = 1;
					break;
				default:
					break;
			}
			show++;
		}
	}

	/* Open the directory */
	ents_list = list_create();
	process_ents = hashmap_create_int(10);

	int flags = PROCFSLIB_NO_FREE;
	if (collect_commandline) flags |= PROCFSLIB_COLLECT_COMMANDLINE;

	procfs_iterate(ps_callback, NULL, flags);

	print_header();
	foreach(entry, ents_list) {
		print_entry(entry->value);
	}


	return 0;
}

