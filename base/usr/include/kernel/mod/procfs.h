#pragma once

#include <kernel/fs.h>

struct procfs_entry {
	int          id;
	char *       name;
	read_type_t  func;
};

extern int procfs_install(struct procfs_entry * entry);
