#pragma once

#include <kernel/vfs.h>

struct procfs_entry {
	intptr_t     id;
	const char *       name;
	read_type_t  func;
};

extern int procfs_install(struct procfs_entry * entry);
extern void procfs_initialize(void);
