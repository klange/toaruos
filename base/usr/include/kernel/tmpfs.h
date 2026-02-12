#pragma once
#include <kernel/vfs.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <sys/types.h>

fs_node_t * tmpfs_create(char * name);

struct tmpfs_file {
	spin_lock_t lock;
	char * name;
	int    type;
	int    mask;
	uid_t  uid;
	uid_t  gid;
	unsigned int atime;
	unsigned int mtime;
	unsigned int ctime;
	fs_node_t * mount;
	size_t length;
	size_t block_count;
	size_t pointers;
	uintptr_t * blocks;
	char * target;
};

struct tmpfs_dir;

struct tmpfs_dir {
	spin_lock_t lock;
	char * name;
	int    type;
	int    mask;
	uid_t  uid;
	uid_t  gid;
	unsigned int atime;
	unsigned int mtime;
	unsigned int ctime;
	fs_node_t * mount;
	list_t * files;
	struct tmpfs_dir * parent;
	spin_lock_t nest_lock;
};

