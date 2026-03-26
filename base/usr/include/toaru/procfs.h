#pragma once

typedef struct process {
	int pid;
	int ppid;
	int tgid;
	char *name;
	char *path;

	int user_data;
} p_t;

#define PROCFSLIB_NO_FREE      1
#define PROCFSLIB_NO_THREADS   2

extern  void procfs_free(struct process * proc);
extern int procfs_iterate(int (*callback)(struct process *,void*), void *ctx, int flags);
