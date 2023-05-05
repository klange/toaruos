/**
 * @file  kernel/vfs/tmpfs.c
 * @brief In-memory read-write filesystem.
 *
 * Generally provides the filesystem for "migrated" live CDs,
 * as well as /tmp and /var.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2021 K. Lange
 */
#include <stdint.h>
#include <errno.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/vfs.h>
#include <kernel/process.h>
#include <kernel/tokenize.h>
#include <kernel/tmpfs.h>
#include <kernel/spinlock.h>
#include <kernel/mmu.h>
#include <kernel/time.h>
#include <kernel/procfs.h>

/* 4KB */
#define BLOCKSIZE 0x1000

#define TMPFS_TYPE_FILE 1
#define TMPFS_TYPE_DIR  2
#define TMPFS_TYPE_LINK 3

static struct tmpfs_dir * tmpfs_root = NULL;
static volatile intptr_t tmpfs_total_blocks = 0;

static fs_node_t * tmpfs_from_dir(struct tmpfs_dir * d);

static struct tmpfs_file * tmpfs_file_new(char * name) {
	struct tmpfs_file * t = malloc(sizeof(struct tmpfs_file));
	spin_init(t->lock);
	t->name = strdup(name);
	t->type = TMPFS_TYPE_FILE;
	t->length = 0;
	t->pointers = 2;
	t->block_count = 0;
	t->mask = 0;
	t->uid = 0;
	t->gid = 0;
	t->atime = now();
	t->mtime = t->atime;
	t->ctime = t->atime;
	t->blocks = malloc(t->pointers * sizeof(char *));
	for (size_t i = 0; i < t->pointers; ++i) {
		t->blocks[i] = 0;
	}

	return t;
}

static int symlink_tmpfs(fs_node_t * parent, char * target, char * name) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)parent->device;

	spin_lock(d->lock);
	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(d->lock);
			return -EEXIST; /* Already exists */
		}
	}
	spin_unlock(d->lock);

	struct tmpfs_file * t = tmpfs_file_new(name);
	t->type = TMPFS_TYPE_LINK;
	t->target = strdup(target);
	t->length = strlen(target);

	t->mask = 0777;
	t->uid = this_core->current_process->user;
	t->gid = this_core->current_process->user;

	spin_lock(d->lock);
	list_insert(d->files, t);
	spin_unlock(d->lock);

	return 0;
}

static ssize_t readlink_tmpfs(fs_node_t * node, char * buf, size_t size) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);

	spin_lock(t->lock);
	if (t->type != TMPFS_TYPE_LINK) {
		spin_unlock(t->lock);
		printf("tmpfs: not a symlink?\n");
		return -1;
	}

	if (size < strlen(t->target) + 1) {
		memcpy(buf, t->target, size-1);
		buf[size-1] = '\0';
		spin_unlock(t->lock);
		return size-2;
	} else {
		size_t len = strlen(t->target);
		memcpy(buf, t->target, len + 1);
		spin_unlock(t->lock);
		return len;
	}
}

static struct tmpfs_dir * tmpfs_dir_new(char * name, struct tmpfs_dir * parent) {
	struct tmpfs_dir * d = malloc(sizeof(struct tmpfs_dir));
	spin_init(d->lock);
	d->name = strdup(name);
	d->type = TMPFS_TYPE_DIR;
	d->mask = 0;
	d->uid = 0;
	d->gid = 0;
	d->atime = now();
	d->mtime = d->atime;
	d->ctime = d->atime;
	d->files = list_create("tmpfs directory entries",d);
	return d;
}

static void tmpfs_file_free(struct tmpfs_file * t) {
	spin_lock(t->lock);
	if (t->type == TMPFS_TYPE_LINK) {
		/* free target string */
		free(t->target);
	}
	for (size_t i = 0; i < t->block_count; ++i) {
		mmu_frame_release((uintptr_t)t->blocks[i] * 0x1000);
		tmpfs_total_blocks--;
	}
	spin_unlock(t->lock);
}

static void tmpfs_file_blocks_embiggen(struct tmpfs_file * t) {
	t->pointers *= 2;
	t->blocks = realloc(t->blocks, sizeof(char *) * t->pointers);
}

static char * tmpfs_file_getset_block(struct tmpfs_file * t, size_t blockid, int create) {
	if (create) {
		while (blockid >= t->pointers) {
			tmpfs_file_blocks_embiggen(t);
		}
		while (blockid >= t->block_count) {
			uintptr_t index = mmu_allocate_a_frame();
			tmpfs_total_blocks++;
			t->blocks[t->block_count] = index;
			t->block_count += 1;
		}
	} else {
		if (blockid >= t->block_count) {
			printf("tmpfs: not enough blocks?\n");
			return NULL;
		}
	}

	return (char *)mmu_map_from_physical(t->blocks[blockid] << 12);
}


static ssize_t read_tmpfs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);

	spin_lock(t->lock);

	t->atime = now();

	uint64_t end;
	if ((size_t)offset + size > t->length) {
		end = t->length;
	} else {
		end = offset + size;
	}
	uint64_t start_block  = offset / BLOCKSIZE;
	uint64_t end_block    = end / BLOCKSIZE;
	uint64_t end_size     = end - end_block * BLOCKSIZE;
	uint64_t size_to_read = end - offset;
	if (start_block == end_block && (size_t)offset == end) {
		spin_unlock(t->lock);
		return 0;
	}
	if (start_block == end_block) {
		void *buf = tmpfs_file_getset_block(t, start_block, 0);
		memcpy(buffer, (uint8_t *)(((uintptr_t)buf) + ((uintptr_t)offset % BLOCKSIZE)), size_to_read);
		spin_unlock(t->lock);
		return size_to_read;
	} else {
		uint64_t block_offset;
		uint64_t blocks_read = 0;
		for (block_offset = start_block; block_offset < end_block; block_offset++, blocks_read++) {
			if (block_offset == start_block) {
				void *buf = tmpfs_file_getset_block(t, block_offset, 0);
				memcpy(buffer, (uint8_t *)(((uint64_t)buf) + ((uintptr_t)offset % BLOCKSIZE)), BLOCKSIZE - (offset % BLOCKSIZE));
			} else {
				void *buf = tmpfs_file_getset_block(t, block_offset, 0);
				memcpy(buffer + BLOCKSIZE * blocks_read - (offset % BLOCKSIZE), buf, BLOCKSIZE);
			}
		}
		if (end_size) {
			void *buf = tmpfs_file_getset_block(t, end_block, 0);
			memcpy(buffer + BLOCKSIZE * blocks_read - (offset % BLOCKSIZE), buf, end_size);
		}
	}
	spin_unlock(t->lock);
	return size_to_read;
}

static ssize_t write_tmpfs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);

	spin_lock(t->lock);
	t->atime = now();
	t->mtime = t->atime;

	uint64_t end;
	if ((size_t)offset + size > t->length) {
		t->length = offset + size;
	}
	end = offset + size;
	uint64_t start_block  = offset / BLOCKSIZE;
	uint64_t end_block    = end / BLOCKSIZE;
	uint64_t end_size     = end - end_block * BLOCKSIZE;
	uint64_t size_to_read = end - offset;
	if (start_block == end_block) {
		void *buf = tmpfs_file_getset_block(t, start_block, 1);
		memcpy((uint8_t *)(((uint64_t)buf) + ((uintptr_t)offset % BLOCKSIZE)), buffer, size_to_read);
		spin_unlock(t->lock);
		return size_to_read;
	} else {
		uint64_t block_offset;
		uint64_t blocks_read = 0;
		for (block_offset = start_block; block_offset < end_block; block_offset++, blocks_read++) {
			if (block_offset == start_block) {
				void *buf = tmpfs_file_getset_block(t, block_offset, 1);
				memcpy((uint8_t *)(((uint64_t)buf) + ((uintptr_t)offset % BLOCKSIZE)), buffer, BLOCKSIZE - (offset % BLOCKSIZE));
			} else {
				void *buf = tmpfs_file_getset_block(t, block_offset, 1);
				memcpy(buf, buffer + BLOCKSIZE * blocks_read - (offset % BLOCKSIZE), BLOCKSIZE);
			}
		}
		if (end_size) {
			void *buf = tmpfs_file_getset_block(t, end_block, 1);
			memcpy(buf, buffer + BLOCKSIZE * blocks_read - (offset % BLOCKSIZE), end_size);
		}
	}
	spin_unlock(t->lock);
	return size_to_read;
}

static int chmod_tmpfs(fs_node_t * node, int mode) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);

	/* XXX permissions */
	t->mask = mode;

	return 0;
}

static int chown_tmpfs(fs_node_t * node, int uid, int gid) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);

	spin_lock(t->lock);
	if (uid != -1) t->uid = uid;
	if (gid != -1) t->gid = gid;
	spin_unlock(t->lock);

	return 0;
}

static int truncate_tmpfs(fs_node_t * node) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);
	spin_lock(t->lock);
	for (size_t i = 0; i < t->block_count; ++i) {
		mmu_frame_release((uintptr_t)t->blocks[i] * 0x1000);
		tmpfs_total_blocks--;
		t->blocks[i] = 0;
	}
	t->block_count = 0;
	t->length = 0;
	t->mtime = node->atime;
	spin_unlock(t->lock);
	return 0;
}

static void open_tmpfs(fs_node_t * node, unsigned int flags) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);

	t->atime = now();
}

static fs_node_t * tmpfs_from_file(struct tmpfs_file * t) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	spin_lock(t->lock);
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, t->name);
	fnode->device = t;
	fnode->mask = t->mask;
	fnode->uid = t->uid;
	fnode->gid = t->gid;
	fnode->atime = t->atime;
	fnode->ctime = t->ctime;
	fnode->mtime = t->mtime;
	fnode->flags   = FS_FILE;
	fnode->read    = read_tmpfs;
	fnode->write   = write_tmpfs;
	fnode->open    = open_tmpfs;
	fnode->close   = NULL;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->chmod   = chmod_tmpfs;
	fnode->chown   = chown_tmpfs;
	fnode->length  = t->length;
	fnode->truncate = truncate_tmpfs;
	fnode->nlink   = 1;
	spin_unlock(t->lock);
	return fnode;
}

static fs_node_t * tmpfs_from_link(struct tmpfs_file * t) {
	fs_node_t * fnode = tmpfs_from_file(t);
	fnode->flags   |= FS_SYMLINK;
	fnode->readlink = readlink_tmpfs;
	fnode->read     = NULL;
	fnode->write    = NULL;
	fnode->create   = NULL;
	fnode->mkdir    = NULL;
	fnode->readdir  = NULL;
	fnode->finddir  = NULL;
	return fnode;
}

static struct dirent * readdir_tmpfs(fs_node_t *node, uint64_t index) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)node->device;
	uint64_t i = 0;

	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, "..");
		return out;
	}

	index -= 2;

	if (index >= d->files->length) return NULL;

	foreach(f, d->files) {
		if (i == index) {
			struct tmpfs_file * t = (struct tmpfs_file *)f->value;
			struct dirent * out = malloc(sizeof(struct dirent));
			memset(out, 0x00, sizeof(struct dirent));
			out->d_ino = (uint64_t)t;
			strcpy(out->d_name, t->name);
			return out;
		} else {
			++i;
		}
	}
	return NULL;
}

static fs_node_t * finddir_tmpfs(fs_node_t * node, char * name) {
	if (!name) return NULL;

	struct tmpfs_dir * d = (struct tmpfs_dir *)node->device;

	spin_lock(d->lock);

	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			fs_node_t * out = NULL;
			switch (t->type) {
				case TMPFS_TYPE_FILE:
					out = tmpfs_from_file(t);
					break;
				case TMPFS_TYPE_LINK:
					out = tmpfs_from_link(t);
					break;
				case TMPFS_TYPE_DIR:
					out = tmpfs_from_dir((struct tmpfs_dir *)t);
					break;
			}
			spin_unlock(d->lock);
			return out;
		}
	}

	spin_unlock(d->lock);
	return NULL;
}


static int try_free_dir(struct tmpfs_dir * d) {
	spin_lock(d->lock);
	if (d->files && d->files->length != 0) {
		spin_unlock(d->lock);
		return 1;
	}
	free(d->files);
	spin_unlock(d->lock);
	return 0;
}

static int unlink_tmpfs(fs_node_t * node, char * name) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)node->device;
	int i = -1, j = 0;

	spin_lock(d->lock);
	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			if (t->type == TMPFS_TYPE_DIR) {
				if (try_free_dir((void*)t)) {
					spin_unlock(d->lock);
					return -ENOTEMPTY;
				}
			} else {
				tmpfs_file_free(t);
			}
			free(t);
			i = j;
			break;
		}
		j++;
	}

	if (i >= 0) {
		list_remove(d->files, i);
	} else {
		spin_unlock(d->lock);
		return -ENOENT;
	}

	spin_unlock(d->lock);
	return 0;
}

static int create_tmpfs(fs_node_t *parent, char *name, mode_t permission) {
	if (!name) return -EINVAL;

	struct tmpfs_dir * d = (struct tmpfs_dir *)parent->device;

	spin_lock(d->lock);
	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(d->lock);
			return -EEXIST; /* Already exists */
		}
	}
	spin_unlock(d->lock);

	struct tmpfs_file * t = tmpfs_file_new(name);
	t->mask = permission;
	t->uid = this_core->current_process->user;
	t->gid = this_core->current_process->user_group;

	spin_lock(d->lock);
	list_insert(d->files, t);
	spin_unlock(d->lock);

	return 0;
}

static int mkdir_tmpfs(fs_node_t * parent, char * name, mode_t permission) {
	if (!name) return -EINVAL;
	if (!strlen(name)) return -EINVAL;

	struct tmpfs_dir * d = (struct tmpfs_dir *)parent->device;

	spin_lock(d->lock);
	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(d->lock);
			return -EEXIST; /* Already exists */
		}
	}
	spin_unlock(d->lock);

	struct tmpfs_dir * out = tmpfs_dir_new(name, d);
	out->mask = permission;
	out->uid  = this_core->current_process->user;
	out->gid  = this_core->current_process->user;

	spin_lock(d->lock);
	list_insert(d->files, out);
	spin_unlock(d->lock);

	return 0;
}

static fs_node_t * tmpfs_from_dir(struct tmpfs_dir * d) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	spin_lock(d->lock);
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "tmp");
	fnode->mask = d->mask;
	fnode->uid  = d->uid;
	fnode->gid  = d->gid;
	fnode->device  = d;
	fnode->atime   = d->atime;
	fnode->mtime   = d->mtime;
	fnode->ctime   = d->ctime;
	fnode->flags   = FS_DIRECTORY;
	fnode->read    = NULL;
	fnode->write   = NULL;
	fnode->open    = NULL;
	fnode->close   = NULL;
	fnode->readdir = readdir_tmpfs;
	fnode->finddir = finddir_tmpfs;
	fnode->create  = create_tmpfs;
	fnode->unlink  = unlink_tmpfs;
	fnode->mkdir   = mkdir_tmpfs;
	fnode->nlink   = 1; /* should be "number of children that are directories + 1" */
	fnode->symlink = symlink_tmpfs;

	fnode->chown   = chown_tmpfs;
	fnode->chmod   = chmod_tmpfs;
	spin_unlock(d->lock);

	return fnode;
}

fs_node_t * tmpfs_create(char * name) {
	tmpfs_root = tmpfs_dir_new(name, NULL);
	tmpfs_root->mask = 0777;
	tmpfs_root->uid  = 0;
	tmpfs_root->gid  = 0;

	return tmpfs_from_dir(tmpfs_root);
}

fs_node_t * tmpfs_mount(const char * device, const char * mount_path) {
	char * arg = strdup(device);
	char * argv[10];
	int argc = tokenize(arg, ",", argv);

	fs_node_t * fs = tmpfs_create(argv[0]);

	if (argc > 1) {
		if (strlen(argv[1]) < 3) {
			printf("tmpfs: ignoring bad permission option for tmpfs\n");
		} else {
			int mode = ((argv[1][0] - '0') << 6) |
			           ((argv[1][1] - '0') << 3) |
			           ((argv[1][2] - '0') << 0);
			fs->mask = mode;
		}
	}

	//free(arg);
	return fs;
}

static void tmpfs_func(fs_node_t * node) {
	procfs_printf(node,
		"UsedBlocks:\t%zd\n",
		tmpfs_total_blocks);
}

static struct procfs_entry tmpfs_entry = {
	0,
	"tmpfs",
	tmpfs_func,
};

void tmpfs_register_init(void) {
	vfs_register("tmpfs", tmpfs_mount);
	procfs_install(&tmpfs_entry);
}

