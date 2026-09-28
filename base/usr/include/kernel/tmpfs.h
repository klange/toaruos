#pragma once
#include <kernel/vfs.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <sys/types.h>

fs_node_t * tmpfs_create(char * name);

struct tmpfs_file {
	fs_node_t _node;
	spin_lock_t lock;
	char * name;
	size_t length;
	size_t block_count;
	size_t pointers;
	uintptr_t * blocks;
	char * target;
};

struct tmpfs_dir;

struct tmpfs_dir {
	fs_node_t _node;
	spin_lock_t lock;
	char * name;
	list_t * files;
	struct tmpfs_dir * parent;
	spin_lock_t nest_lock;
};

