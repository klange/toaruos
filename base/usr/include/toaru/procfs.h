#pragma once

#include <stdint.h>

typedef struct process {
	int pid;
	int ppid;
	int tgid;
	int pgid;
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

#define PROCFSLIB_NO_FREE              1 /* Don't free the p_t's after the callback handles them */
#define PROCFSLIB_NO_THREADS           2 /* Don't include threads. CPU usage might be inaccurate. */
#define PROCFSLIB_COLLECT_COMMANDLINE  4 /* Collect the full process commandline (from /proc/{pid}/cmdline) */
#define PROCFSLIB_COLLECT_STARTTIME    8 /* Collect the start time (the /proc/{pid}'s ctime) */
#define PROCFSLIB_NO_CURLY_THREADS    16 /* Don't wrap thread names in {curly brackets} */

extern  void procfs_free(struct process * proc);
extern int procfs_iterate(int (*callback)(struct process *,void*), void *ctx, int flags);
extern struct process * procfs_get_pid(pid_t pid, int flags);
