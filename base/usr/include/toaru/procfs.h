#pragma once

#include <stdint.h>

typedef struct process {
	int pid;
	int ppid;
	int tgid;
	char *name;
	char *path;

	int user_data;

	int uid, mem, vsz, shm, cpu;
	unsigned long time;
	char * cmdline;
	size_t cmdline_len;
} p_t;

#define PROCFSLIB_NO_FREE             1
#define PROCFSLIB_NO_THREADS          2
#define PROCFSLIB_COLLECT_COMMANDLINE 4

extern  void procfs_free(struct process * proc);
extern int procfs_iterate(int (*callback)(struct process *,void*), void *ctx, int flags);
