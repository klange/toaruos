#pragma once

#include <kernel/vfs.h>

typedef void (*procfs_populate_t)(fs_node_t * node);

struct procfs_entry {
	intptr_t     id;
	const char *       name;
	procfs_populate_t func;
	int flags;
};

typedef struct procfs_entry_node {
	fs_node_t fnode;
	char * buf;
	size_t avail;
	size_t used;
	procfs_populate_t func;
	list_t * files;
} procfs_entry_t;

extern int procfs_install(struct procfs_entry * entry);
extern void procfs_initialize(void);
extern int procfs_printf(fs_node_t * node, const char * fmt, ...);
