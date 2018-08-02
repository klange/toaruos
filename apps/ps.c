/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * ps
 *
 * print a list of running processes
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

#define LINE_LEN 4096

static int show_all = 0;
static int show_threads = 0;
static int show_username = 0;
static int collect_commandline = 0;

static int widths[5] = {3,3,4,0,0};

struct process {
	int uid;
	int pid;
	int tid;
	char * process;
	char * command_line;
};

void print_username(int uid) {
	struct passwd * p = getpwuid(uid);

	if (p) {
		printf("%-8s", p->pw_name);
	} else {
		printf("%-8d", uid);
	}

	endpwent();
}

struct process * process_entry(struct dirent *dent) {
	char tmp[256];
	FILE * f;
	char line[LINE_LEN];

	int pid = 0, uid = 0, tgid = 0;
	char name[100];

	sprintf(tmp, "/proc/%s/status", dent->d_name);
	f = fopen(tmp, "r");

	if (!f) {
		return NULL;
	}

	line[0] = 0;

	while (fgets(line, LINE_LEN, f) != NULL) {
		char * n = strstr(line,"\n");
		if (n) { *n = '\0'; }
		char * tab = strstr(line,"\t");
		if (tab) {
			*tab = '\0';
			tab++;
		}
		if (strstr(line, "Pid:") == line) {
			pid = atoi(tab);
		} else if (strstr(line, "Uid:") == line) {
			uid = atoi(tab);
		} else if (strstr(line, "Tgid:") == line) {
			tgid = atoi(tab);
		} else if (strstr(line, "Name:") == line) {
			strcpy(name, tab);
		}
	}

	fclose(f);

	if (!show_all) {
		/* Filter not ours */
		if (uid != getuid()) return NULL;
	}

	if (!show_threads) {
		if (tgid != pid) return NULL;
	}

	struct process * out = malloc(sizeof(struct process));
	out->uid = uid;
	out->pid = tgid;
	out->tid = pid;
	out->process = strdup(name);
	out->command_line = NULL;

	char garbage[1024];
	int len;

	if ((len = sprintf(garbage, "%d", out->pid)) > widths[0]) widths[0] = len;
	if ((len = sprintf(garbage, "%d", out->tid)) > widths[1]) widths[1] = len;

	struct passwd * p = getpwuid(out->uid);
	if (p) {
		if ((len = strlen(p->pw_name)) > widths[2]) widths[2] = len;
	} else {
		if ((len = sprintf(garbage, "%d", out->uid)) > widths[2]) widths[2] = len;
	}
	endpwent();

	if (collect_commandline) {
		sprintf(tmp, "/proc/%s/cmdline", dent->d_name);
		f = fopen(tmp, "r");
		char foo[1024];
		int s = fread(foo, 1, 1024, f);
		if (s > 0) {
			out->command_line = malloc(s + 1);
			memset(out->command_line, 0, s + 1);
			memcpy(out->command_line, foo, s);

			for (int i = 0; i < s; ++i) {
				if (out->command_line[i] == 30) {
					out->command_line[i] = ' ';
				}
			}

		}
		fclose(f);
	}

	return out;
}

void print_header(void) {
	if (show_username) {
		printf("%-*s ", widths[2], "USER");
	}
	printf("%*s ", widths[0], "PID");
	if (show_threads) {
		printf("%*s ", widths[1], "TID");
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
	printf("%*d ", widths[0], out->pid);
	if (show_threads) {
		printf("%*d ", widths[1], out->tid);
	}
	if (out->command_line) {
		printf("%s\n", out->command_line);
	} else {
		printf("%s\n", out->process);
	}
}

void show_usage(int argc, char * argv[]) {
	printf(
			"ps - list running processes\n"
			"\n"
			"usage: %s [-A] [format]\n"
			"\n"
			" -A     \033[3mshow other users' processes\033[0m\n"
			" -T     \033[3mshow threads\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

int main (int argc, char * argv[]) {

	/* Parse arguments */
	char c;
	while ((c = getopt(argc, argv, "AT?")) != -1) {
		switch (c) {
			case 'A':
				show_all = 1;
				break;
			case 'T':
				show_threads = 1;
				break;
			case '?':
				show_usage(argc, argv);
				return 0;
		}
	}

	if (optind < argc) {
		char * show = argv[optind];
		while (*show) {
			switch (*show) {
				case 'u':
					show_username = 1;
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
	DIR * dirp = opendir("/proc");

	/* Read the entries in the directory */
	list_t * ents_list = list_create();

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
			struct process * p = process_entry(ent);
			if (p) {
				list_insert(ents_list, (void *)p);
			}
		}

		ent = readdir(dirp);
	}
	closedir(dirp);

	print_header();
	foreach(entry, ents_list) {
		print_entry(entry->value);
	}


	return 0;
}

/*
 * vim: tabstop=4
 * vim: shiftwidth=4
 * vim: noexpandtab
 */
