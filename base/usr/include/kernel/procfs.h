#pragma once

#include <kernel/vfs.h>

typedef void (*procfs_populate_t)(fs_node_t * node);

struct procfs_entry {
	intptr_t     id;
	const char *       name;
	procfs_populate_t func;
};

extern int procfs_install(struct procfs_entry * entry);
extern void procfs_initialize(void);
extern int procfs_printf(fs_node_t * node, const char * fmt, ...);
