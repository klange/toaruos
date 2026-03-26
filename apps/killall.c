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
	char name[100];
	char path[200];
} p_t;

#define LINE_LEN 4096

p_t * build_entry(struct dirent * dent) {
	char tmp[300];
	FILE * f;
	char line[LINE_LEN];

	sprintf(tmp, "/proc/%s/status", dent->d_name);
	f = fopen(tmp, "r");

	p_t * proc = malloc(sizeof(p_t));

	while (fgets(line, LINE_LEN, f) != NULL) {
		char * n = strstr(line,"\n");
		if (n) { *n = '\0'; }
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
			strcpy(proc->name, tab);
		} else if (strstr(line, "Path:") == line) {
			strcpy(proc->path, tab);
		}
	}

	if (strstr(proc->name,"python") == proc->name) {
		char * name = proc->path + strlen(proc->path) - 1;

		while (1) {
			if (*name == '/') {
				name++;
				break;
			}
			if (name == proc->name) break;
			name--;
		}

		memcpy(proc->name, name, strlen(name)+1);
	}

	if (proc->tgid != proc->pid) {
		char tmp[100] = {0};
		sprintf(tmp, "{%s}", proc->name);
		memcpy(proc->name, tmp, strlen(tmp)+1);
	}

	fclose(f);

	return proc;
}

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

int main (int argc, char * argv[]) {

	int signum = SIGTERM;

	int c;
	while ((c = getopt(argc, argv, "s:-:")) != -1) {
		switch (c) {
			case 's':
				for (char *s = optarg; *s; s++) {
					*s = toupper(*s);
				}
				if (str2sig(optarg, &signum)) {
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

	int killed_something = 0;
	int retval = 0;

	for (int i = optind; i < argc; ++i) {
		/* Open the directory */
		DIR * dirp = opendir("/proc");

		struct dirent * ent = readdir(dirp);
		while (ent != NULL) {
			if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
				p_t * proc = build_entry(ent);

				if (!strcmp(proc->name, argv[i])) {
					if (kill(proc->pid, signum) < 0) {
						fprintf(stderr, "%s(%d) %s\n", argv[i], proc->pid, strerror(errno));
					} else {
						killed_something = 1;
					}
				}

				free(proc);
			}
			ent = readdir(dirp);
		}
		closedir(dirp);
		if (!killed_something) {
			fprintf(stderr, "%s: no process found\n", argv[i]);
			retval = 1;
		}
	}

	return retval;
}

