/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
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
#include <syscall.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

#include "lib/list.h"

#define LINE_LEN 4096

static int show_all = 0;

void print_username(int uid) {
	struct passwd * p = getpwuid(uid);

	if (p) {
		printf("%-8s", p->pw_name);
	} else {
		printf("%-8d", uid);
	}

	endpwent();
}

void print_entry(struct dirent * dent) {
	char tmp[256], buf[4096];
	FILE * f;
	int read = 1;
	char line[LINE_LEN];

	int pid, uid, tgid;
	char name[100];

	sprintf(tmp, "/proc/%s/status", dent->d_name);
	f = fopen(tmp, "r");

	if (!f) {
		return;
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

	if ((tgid != pid) && !show_all) {
		/* Skip threads */
		return;
	}

	print_username(uid);
	if (show_all) {
		printf("%5d.%-5d", tgid, pid);
	} else {
		printf(" %5d", pid);
	}

	printf(" ");

	sprintf(tmp, "/proc/%s/cmdline", dent->d_name);
	f = fopen(tmp, "r");
	memset(buf, 0x00, 4096);
	read = fread(buf, 1, 4096, f);
	fclose(f);

	buf[read] = '\0';
	for (int i = 0; i < read; ++i) {
		if (buf[i] == '\036') {
			buf[i] = ' ';
		}
	}

	if (tgid != pid) {
		printf("{%s}\n", buf);
	} else {
		printf("%s\n", buf);
	}
}

void show_usage(int argc, char * argv[]) {
	printf(
			"ps - list running processes\n"
			"\n"
			"usage: %s [-A] [format]\n"
			"\n"
			" -A     \033[3mignored\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

int main (int argc, char * argv[]) {

	/* Parse arguments */

	if (argc > 1) {
		for (int i = 1; i < argc; ++i) {
			if (argv[i][0] == '-') {
				char *c = &argv[i][1];
				while (*c) {
					switch (*c) {
						case 'A':
							show_all = 1;
							break;
						case '?':
							show_usage(argc, argv);
							return 0;
					}
					c++;
				}
			}
		}
	}

	/* Open the directory */
	DIR * dirp = opendir("/proc");

	/* Read the entries in the directory */
	list_t * ents_list = list_create();

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
			struct dirent * entcpy = malloc(sizeof(struct dirent));
			memcpy(entcpy, ent, sizeof(struct dirent));
			list_insert(ents_list, (void *)entcpy);
		}

		ent = readdir(dirp);
	}
	closedir(dirp);

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
