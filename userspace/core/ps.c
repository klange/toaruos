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

	int pid, uid;

	snprintf(tmp, 256, "/proc/%s/status", dent->d_name);
	f = fopen(tmp, "r");

	while (fgets(line, LINE_LEN, f) != NULL) {
		if (strstr(line, "Pid:") == line) {
			sscanf(line, "%s %d", &buf, &pid);
		} else if (strstr(line, "Uid:") == line) {
			sscanf(line, "%s %d", &buf, &uid);
		}
	}

	fclose(f);

	print_username(uid);
	printf(" %5d ", pid);

	snprintf(tmp, 256, "/proc/%s/cmdline", dent->d_name);
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

	printf("%s\n", buf);
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
	int show_all = 0;

	if (argc > 1) {
		int index, c;
		while ((c = getopt(argc, argv, "A?")) != -1) {
			switch (c) {
				case 'A':
					show_all = 1;
					break;
				case '?':
					show_usage(argc, argv);
					return 0;
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
