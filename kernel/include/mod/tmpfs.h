#ifndef _TMPFS_H__
#define _TMPFS_H__
#include <fs.h>

fs_node_t * tmpfs_create(char * name);

struct tmpfs_file {
	char * name;
	int    type;
	int    mask;
	int    uid;
	int    gid;
	unsigned int atime;
	unsigned int mtime;
	unsigned int ctime;
	size_t length;
	size_t block_count;
	size_t pointers;
	char ** blocks;
};

struct tmpfs_dir;

struct tmpfs_dir {
	char * name;
	int    type;
	int    mask;
	int    uid;
	int    gid;
	unsigned int atime;
	unsigned int mtime;
	unsigned int ctime;
	list_t * files;
	struct tmpfs_dir * parent;
};

#endif /* _TMPFS_H__ */
