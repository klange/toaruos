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
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

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

	if (!f) return NULL;

	p_t * proc = calloc(1, sizeof(p_t));

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
		} else if (strstr(line, "Pgid:") == line) {
			proc->pgid = atoi(tab);
		} else if (strstr(line, "Name:") == line) {
			proc->name = strdup(tab);
		} else if (strstr(line, "Uid:") == line) {
			proc->uid = atoi(tab);
		} else if (strstr(line, "VmSize:") == line) {
			proc->vsz = atoi(tab);
		} else if (strstr(line, "VmRSS:") == line) {
			proc->res = atoi(tab);
		} else if (strstr(line, "RssShmem:") == line) {
			proc->shm = atoi(tab);
		} else if (strstr(line, "MemPermille:") == line) {
			proc->mem = atoi(tab);
		} else if (strstr(line, "TotalTime:") == line) {
			proc->time = strtoul(tab,NULL,0);
		} else if (strstr(line, "State:") == line) {
			proc->state = strdup(tab);
		} else if (strstr(line, "Tty:") == line) {
			proc->tty = strdup(tab);
		}
	}

	fclose(f);
	if (line) free(line);

	if (!proc->name) proc->name = strdup("");
	if (!proc->state) proc->state = strdup("");
	if (!proc->tty) proc->tty = strdup("");

	if (proc->tgid != proc->pid && !(flags & PROCFSLIB_NO_CURLY_THREADS)) {
		char * tmp;
		asprintf(&tmp, "{%s}", proc->name);
		free(proc->name);
		proc->name = tmp;
	}

	if (flags & PROCFSLIB_COLLECT_COMMANDLINE) {
		char * tmp = NULL;
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

	if (flags & PROCFSLIB_COLLECT_EXE) {
		char * tmp = NULL;
		asprintf(&tmp, "/proc/%s/exe", dent->d_name);
		char exe_path[4096] = {0};
		ssize_t r = readlink(tmp, exe_path, 4096);
		if (r != 4096 && r > 0) {
			proc->exe = strdup(exe_path);
		}
		free(tmp);
	}

	if (flags & PROCFSLIB_COLLECT_STARTTIME) {
		struct stat st;
		char * tmp;
		asprintf(&tmp, "/proc/%s", dent->d_name);
		if (stat(tmp, &st) == 0) {
			proc->starttime = st.st_ctime;
			proc->starttimensec = st.st_ctim.tv_nsec;
		}
		free(tmp);
	}

	return proc;
}

void procfs_free(struct process * proc) {
	free(proc->name);
	free(proc->state);
	free(proc->tty);
	if (proc->cmdline) free(proc->cmdline);
	if (proc->exe) free(proc->exe);
	free(proc);
}

static int procfs_iterate_dir(int (*callback)(struct process *,void*), void *ctx, int flags, char * dir, int recur) {
	int ret = 0;
	DIR * dirp = opendir(dir);

	for (struct dirent * ent = readdir(dirp); ent; ent = readdir(dirp)) {
		if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
			p_t * proc = build_entry(ent, flags);
			if (!proc) continue;
			if (!recur && proc->pid == proc->tgid) {
				procfs_free(proc);
				continue;
			}
			if ((flags & PROCFSLIB_NO_THREADS) && proc->pid != proc->tgid) {
				procfs_free(proc);
				continue;
			}
			if (callback(proc,ctx)) ret = 1;
			if (!(flags & PROCFSLIB_NO_FREE)) procfs_free(proc);
			if (recur && !(flags & PROCFSLIB_NO_THREADS)) {
				char tasks_dir[1000];
				snprintf(tasks_dir, 1000, "%s/%s/task", dir, ent->d_name);
				if (procfs_iterate_dir(callback, ctx, flags, tasks_dir, 0)) ret = 1;
			}
		}
		if (ret) break;
	}
	closedir(dirp);
	return ret;
}

int procfs_iterate(int (*callback)(struct process *,void*), void *ctx, int flags) {
	return procfs_iterate_dir(callback, ctx, flags, "/proc", 1);
}

struct process * procfs_get_pid(pid_t pid, int flags) {
	struct dirent dent;
	snprintf(dent.d_name, sizeof(dent.d_name), "%d", pid);
	return build_entry(&dent, flags);
}

int procfs_get_cpu_info(struct procfs_cpu_info *cpus, int max_cpus) {
	FILE * f = fopen("/proc/stat","re");

	if (!f) return 1;

	char *line = NULL;
	size_t avail = 0;
	ssize_t len = 0;

	int cpu = 0;

	while ((len = getline(&line, &avail, f)) != -1) {
		if (!strncmp(line, "cpu", 3) && line[3] != ' ') {
			/* Assume these are in order */
			char * x = strchr(line, ' ');
			if (!x) continue;
			char * user_time = ++x;
			x = strchr(x, ' ');
			if (!x) continue;
			*x = '\0';
			++x; /* unused, nice time */
			x = strchr(x, ' ');
			if (!x) continue;
			*x = '\0';
			char * sys_time = ++x;
			x = strchr(x, ' ');
			if (!x) continue;
			*x = '\0';
			char * idle_time = ++x;
			x = strchr(x, ' ');
			if (!x) continue;
			*x = '\0';

			cpus[cpu].user = strtoul(user_time, NULL, 0);
			cpus[cpu].sys  = strtoul(sys_time,  NULL, 0);
			cpus[cpu].idle = strtoul(idle_time, NULL, 0);

			cpus[cpu].total = cpus[cpu].user + cpus[cpu].sys + cpus[cpu].idle;
			cpus[cpu].used  = cpus[cpu].user + cpus[cpu].sys;

			cpu++;
			if (cpu == max_cpus) break;
		}
	}

	free(line);
	fclose(f);

	return 0;
}

