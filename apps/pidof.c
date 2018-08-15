/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * pidof - Find and print process IDs
 *
 */
#include <sys/stat.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>

typedef struct process {
	int pid;
	int ppid;
	int tgid;
	char name[100];
	char path[200];
} p_t;

#define LINE_LEN 4096

p_t * build_entry(struct dirent * dent) {
	char tmp[256];
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

int main (int argc, char * argv[]) {

	if (argc < 2) return 1;

	int space = 0;

	/* Open the directory */
	DIR * dirp = opendir("/proc");

	int found_something = 0;

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
			p_t * proc = build_entry(ent);

			if (!strcmp(proc->name, argv[optind])) {
				if (space++) printf(" ");
				printf("%d", proc->pid);
				found_something = 1;
			}
		}
		ent = readdir(dirp);
	}
	closedir(dirp);

	if (!found_something) {
		return 1;
	}
	printf("\n");
	return 0;
}


