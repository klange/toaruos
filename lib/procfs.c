/**
 * @brief A helper library for dealing with ToaruOS's procfs.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include <toaru/procfs.h>

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

