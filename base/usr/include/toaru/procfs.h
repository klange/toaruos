#pragma once

#include <stdint.h>

typedef struct process {
	int pid;
	int ppid;
	int tgid;
	int uid, mem, vsz, shm;
	int cpu[4];
	unsigned long time;
	char *name;
	char *path;
	char * state;
	time_t starttime;

	char * cmdline;
	size_t cmdline_len;

	int user_data;
	void * user_pdata;
} p_t;

#define PROCFSLIB_NO_FREE             1
#define PROCFSLIB_NO_THREADS          2
#define PROCFSLIB_COLLECT_COMMANDLINE 4
#define PROCFSLIB_COLLECT_STARTTIME   8

extern  void procfs_free(struct process * proc);
extern int procfs_iterate(int (*callback)(struct process *,void*), void *ctx, int flags);
