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

static ssize_t read_file(char **restrict data, FILE *restrict f) {
	size_t c = 0;
	size_t n = 0;

	while (1) {
		int i = fgetc(f);
		if (n <= c + 1) {
			size_t nn = n < 20 ? 20 : (n * 2);
			if (nn > LONG_MAX) nn = LONG_MAX;
			if (nn <= c + 1) {
				return -1;
			}
			char * ndata = realloc(*data, nn);
			n = nn;
			*data = ndata;
		}

		if (i == EOF) {
			(*data)[c] = '\0';
			return c;
		}

		(*data)[c++] = i;
	}
}

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
		} else if (strstr(line, "Uid:") == line) {
			proc->uid = atoi(tab);
		} else if (strstr(line, "VmSize:") == line) {
			proc->vsz = atoi(tab);
		} else if (strstr(line, "RssShmem:") == line) {
			proc->shm = atoi(tab);
		} else if (strstr(line, "MemPermille:") == line) {
			proc->mem = atoi(tab);
		} else if (strstr(line, "CpuPermille:") == line) {
			proc->cpu[0] = strtoul(tab, &tab, 10);
			proc->cpu[1] = strtoul(tab, &tab, 10);
			proc->cpu[2] = strtoul(tab, &tab, 10);
			proc->cpu[3] = strtoul(tab, &tab, 10);
		} else if (strstr(line, "TotalTime:") == line) {
			proc->time = strtoul(tab,NULL,0);
		} else if (strstr(line, "State:") == line) {
			proc->state = strdup(tab);
		}
	}

	fclose(f);
	if (line) free(line);

	if (!proc->name) proc->name = strdup("");
	if (!proc->path) proc->path = strdup("");
	if (!proc->state) proc->state = strdup("");

	if (proc->tgid != proc->pid) {
		char * tmp;
		asprintf(&tmp, "{%s}", proc->name);
		free(proc->name);
		proc->name = tmp;
	}

	if (flags & PROCFSLIB_COLLECT_COMMANDLINE) {
		char * tmp;
		asprintf(&tmp, "/proc/%s/cmdline", dent->d_name);
		f = fopen(tmp, "r");
		free(tmp);

		if (f) {
			char * data = NULL;
			ssize_t len = read_file(&data, f);
			if (len >= 0) {
				proc->cmdline = data;
				proc->cmdline_len = len;
			} else if (data) {
				free(data);
			}
			fclose(f);
		}
	}


	return proc;
}

void procfs_free(struct process * proc) {
	free(proc->name);
	free(proc->path);
	free(proc->state);
	if (proc->cmdline) free(proc->cmdline);
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

