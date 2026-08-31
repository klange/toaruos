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
#include <bits/errno.h>
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
#include <kernel/mman.h>
#include <sys/mman.h>

/* 4KB */
#define BLOCKSIZE 0x1000

#define TMPFS_TYPE_FILE 1
#define TMPFS_TYPE_DIR  2
#define TMPFS_TYPE_LINK 3

static volatile intptr_t tmpfs_total_blocks = 0;
static volatile size_t   tmpfs_ino_counter = 1;

static fs_node_t * tmpfs_from_dir(struct tmpfs_dir * d);

static struct tmpfs_file * tmpfs_file_new(const char * name) {
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
	t->blocks = calloc(t->pointers, sizeof(char *));
	t->ino = tmpfs_ino_counter++;

	return t;
}

static int symlink_tmpfs(fs_node_t * parent, const char * target, const char * name) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)parent->impl;

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
	t->mount = parent->mount;
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
	struct tmpfs_file * t = (struct tmpfs_file *)(node->impl);

	spin_lock(t->lock);
	if (t->type != TMPFS_TYPE_LINK) {
		spin_unlock(t->lock);
		return -EINVAL;
	}

	size_t len = strlen(t->target);
	if (size < len) len = size;
	memcpy(buf, t->target, len);
	spin_unlock(t->lock);
	return len;
}

static struct tmpfs_dir * tmpfs_dir_new(const char * name, struct tmpfs_dir * parent) {
	struct tmpfs_dir * d = calloc(1, sizeof(struct tmpfs_dir));
	spin_init(d->lock);
	spin_init(d->nest_lock);
	d->mount = parent ? parent->mount : NULL;
	d->parent = parent;
	d->name = strdup(name);
	d->type = TMPFS_TYPE_DIR;
	d->mask = 0;
	d->uid = 0;
	d->gid = 0;
	d->atime = now();
	d->mtime = d->atime;
	d->ctime = d->atime;
	d->files = list_create("tmpfs directory entries",d);
	d->ino = tmpfs_ino_counter++;
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
			if (create == 2) {
				memset((char*)mmu_map_from_physical(index << 12), 0, BLOCKSIZE);
			}
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

static uint64_t tmpfs_ext_getblock(struct tmpfs_file *t, off_t offset) {
	spin_lock(t->lock);
	uint64_t block = offset / BLOCKSIZE;
	uint64_t page = (block < t->block_count) ? t->blocks[block] : 0;
	spin_unlock(t->lock);
	return page;
}

static ssize_t read_tmpfs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->impl);

	spin_lock(t->lock);

	t->atime = now();

	if ((size_t)offset >= t->length) {
		spin_unlock(t->lock);
		return 0;
	}

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
	struct tmpfs_file * t = (struct tmpfs_file *)(node->impl);

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
	struct tmpfs_file * t = (struct tmpfs_file *)(node->impl);

	/* XXX permissions */
	t->mask = mode;
	node->mask = mode;

	return 0;
}

static int chown_tmpfs(fs_node_t * node, int uid, int gid) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->impl);

	spin_lock(t->lock);
	if (uid != -1) t->uid = uid;
	if (gid != -1) t->gid = gid;
	spin_unlock(t->lock);

	return 0;
}

static int utimens_tmpfs(fs_node_t * node, struct timespec access, struct timespec modify) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->impl);

	if (access.tv_sec != 0 || access.tv_nsec != 0) {
		t->atime = access.tv_sec;
		node->atime = access.tv_sec;
	}

	if (modify.tv_sec != 0 || modify.tv_nsec != 0) {
		t->mtime = modify.tv_sec;
		node->mtime = modify.tv_sec;
	}

	return 0;
}


static int truncate_tmpfs(fs_node_t * node, size_t size) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->impl);
	spin_lock(t->lock);

	if (size == t->length) goto _exit_truncate;

	uint64_t old_end_block = (t->length / BLOCKSIZE);
	uint64_t old_end_size  = t->length - old_end_block * BLOCKSIZE;
	uint64_t old_blocks = old_end_block + !!old_end_size;
	uint64_t new_end_block = (size / BLOCKSIZE);
	uint64_t new_end_size  = size - new_end_block * BLOCKSIZE;
	uint64_t new_blocks = new_end_block + !!new_end_size;


	/* Is the target size bigger or smaller? */
	if (size > t->length) {
		if (old_end_block == new_end_block) {
			char *buf = tmpfs_file_getset_block(t, old_end_block, old_end_size ? 0 : 2);
			memset(buf + old_end_size, 0, new_end_size - old_end_size);
		} else {
			tmpfs_file_getset_block(t, new_end_block, 2);
			char *buf = tmpfs_file_getset_block(t, old_end_block, 0);
			memset(buf + old_end_size, 0, BLOCKSIZE - old_end_size);
		}
		t->length = size;
		goto _exit_truncate;
	}

	if (size == 0) {
		for (size_t i = 0; i < t->block_count; ++i) {
			mmu_frame_release((uintptr_t)t->blocks[i] * 0x1000);
			tmpfs_total_blocks--;
			t->blocks[i] = 0;
		}
		t->block_count = 0;
		t->length = 0;
		goto _exit_truncate;
	}

	/* Size is less than current but > 0 */
	if (new_blocks < old_blocks) {
		for (uint64_t i = new_blocks; i < old_blocks; ++i) {
			mmu_frame_release((uintptr_t)t->blocks[i] * 0x1000);
			tmpfs_total_blocks--;
			t->blocks[i] = 0;
		}
		t->block_count = new_blocks;
	}

	t->length = size;

_exit_truncate:
	t->mtime = node->atime;
	spin_unlock(t->lock);
	return 0;
}

static void open_tmpfs(fs_node_t * node, unsigned int flags) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->impl);

	t->atime = now();
}

static ssize_t get_size_tmpfs(fs_node_t * node) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->impl);
	return t->length;
}

static int fault_map_tmpfs(fs_node_t * node, union PML * page, off_t offset, int fault_flags, int map_flags, int prot, int *mmu_flags) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->impl);

	if (map_flags & MAP_SHARED) {
		if (!(prot & PROT_WRITE) && (fault_flags & FAULT_CODE_WRITE)) return 1; /* Should be rejected earlier? */
		uint64_t fpage = tmpfs_ext_getblock(t, offset);
		if (!fpage) return 1; /* Request out of bounds */
		page->bits.page = fpage;
		page->bits.mmap_shared = 1;
		if (!(prot & PROT_WRITE)) (*mmu_flags) &= ~(MMU_FLAG_WRITABLE);
		return 0;
	}

	if (!(fault_flags & FAULT_CODE_WRITE)) {
		uint64_t fpage = tmpfs_ext_getblock(t, offset);
		if (!fpage) return 1;
		page->bits.page = fpage;
		page->bits.mmap_shared = 1;
		(*mmu_flags) &= ~(MMU_FLAG_WRITABLE);
		return 0;
	}

	/* write request or out of range, defer */
	return 1;
}

static fs_vtable_t tmpfs_file_ops = {
	.read    = read_tmpfs,
	.write   = write_tmpfs,
	.open    = open_tmpfs,
	.chmod   = chmod_tmpfs,
	.chown   = chown_tmpfs,
	.truncate = truncate_tmpfs,
	.get_size = get_size_tmpfs,
	.fault_map = fault_map_tmpfs,
	.utimens = utimens_tmpfs,
};

static fs_node_t * tmpfs_from_file(struct tmpfs_file * t) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	spin_lock(t->lock);
	memset(fnode, 0x00, sizeof(fs_node_t));
	strcpy(fnode->name, t->name);
	fnode->impl = (uintptr_t)t;
	fnode->inode = t->ino;
	fnode->mask = t->mask;
	fnode->uid = t->uid;
	fnode->gid = t->gid;
	fnode->atime = t->atime;
	fnode->ctime = t->ctime;
	fnode->mtime = t->mtime;
	fnode->flags   = FS_FILE;
	fnode->length  = t->length;
	fnode->nlink   = 1;
	fnode->mount   = t->mount;
	fnode->device  = t->mount;
	fnode->ops = &tmpfs_file_ops;
	spin_unlock(t->lock);
	return fnode;
}

static fs_vtable_t tmpfs_link_ops = {
	.readlink = readlink_tmpfs,
};

static fs_node_t * tmpfs_from_link(struct tmpfs_file * t) {
	fs_node_t * fnode = tmpfs_from_file(t);
	fnode->flags   |= FS_SYMLINK;
	fnode->ops = &tmpfs_link_ops;
	return fnode;
}

static int readdir_tmpfs(fs_node_t *node, uint64_t index, struct dirent * out) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)node->impl;
	uint64_t i = 0;

	if (index == 0) {
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, ".");
		return 1;
	}

	if (index == 1) {
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, "..");
		return 1;
	}

	index -= 2;

	if (index >= d->files->length) return 0;

	foreach(f, d->files) {
		if (i == index) {
			struct tmpfs_file * t = (struct tmpfs_file *)f->value;
			memset(out, 0x00, sizeof(struct dirent));
			out->d_ino = (uint64_t)t;
			strcpy(out->d_name, t->name);
			return 1;
		} else {
			++i;
		}
	}
	return 0;
}

static fs_node_t * finddir_tmpfs(fs_node_t * node, const char * name) {
	if (!name) return NULL;

	struct tmpfs_dir * d = (struct tmpfs_dir *)node->impl;

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

static int sticky_check(fs_node_t * node, struct tmpfs_dir * d, struct tmpfs_file *t) {
	if (!(node->mask & S_ISVTX)) return 0;
	uid_t me = this_core->current_process->user;
	if (me == 0 || me == d->uid || me == t->uid) return 0;
	return 1;
}

static int unlink_tmpfs(fs_node_t * node, const char * name) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)node->impl;
	int i = -1, j = 0;

	spin_lock(d->lock);
	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			if (sticky_check(node, d, t)) {
				spin_unlock(d->lock);
				return -EPERM;
			}
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

static int create_tmpfs(fs_node_t *parent, const char *name, mode_t permission, fs_node_t ** out) {
	if (!name) return -EINVAL;

	struct tmpfs_dir * d = (struct tmpfs_dir *)parent->impl;

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
	t->mount = parent->mount;
	t->mask = permission;
	t->uid = this_core->current_process->user;
	t->gid = this_core->current_process->user_group;

	spin_lock(d->lock);
	list_insert(d->files, t);
	*out = tmpfs_from_file(t);
	spin_unlock(d->lock);

	return 0;
}

static int mkdir_tmpfs(fs_node_t * parent, const char * name, mode_t permission, fs_node_t ** out_node) {
	if (!name) return -EINVAL;
	if (!strlen(name)) return -EINVAL;

	struct tmpfs_dir * d = (struct tmpfs_dir *)parent->impl;

	spin_lock(d->lock);
	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(d->lock);
			return -EEXIST; /* Already exists */
		}
	}
	spin_unlock(d->lock);

	/* Need both exec and write on the parent to create a new entry */
	if (!has_permission(parent, W_OK|X_OK)) {
		return -EACCES;
	}

	struct tmpfs_dir * out = tmpfs_dir_new(name, d);
	out->mask = permission;
	out->uid  = this_core->current_process->user;
	out->gid  = this_core->current_process->user;

	spin_lock(d->lock);
	list_insert(d->files, out);
	if (out_node) *out_node = tmpfs_from_dir(out);
	spin_unlock(d->lock);

	return 0;
}

static int path_comp(const char * a, const char * b) {
	while (*a && *b && *a != '/' && *b != '/') {
		if (*a != *b) return 1;
		a++;
		b++;
	}

	if ((*a == '/' || !*a) && (*b == '/' || !*b)) return 0;
	return 1;
}

static int endswith(const char * str, char ch) {
	size_t len = strlen(str);
	if (len > 1 && str[len-1] == ch) return 1;
	return 0;
}

static char * path_dup(const char * path) {
	const char * n = path;
	while (*n && *n != '/') n++;
	char * out = malloc(n - path + 1);
	memcpy(out, path, n - path);
	out[n-path] = '\0';
	return out;
}


static int rename_tmpfs(fs_node_t * mount_root, fs_node_t * src_dir, const char * src_name, fs_node_t * dest_dir, const char * dest_name) {
	/* src_dir and dest_dir are definitely from us, no worries there */
	int ret = 0;

	struct tmpfs_dir * root = (struct tmpfs_dir*)mount_root->impl;
	spin_lock(root->nest_lock);

	struct tmpfs_dir * ds = (struct tmpfs_dir *)src_dir->impl;
	spin_lock(ds->lock);

	/* First, get the source file */
	struct tmpfs_file * src_file = NULL;
	node_t * src_node = NULL;
	foreach(f, ds->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!path_comp(src_name, t->name)) {
			src_file = t;
			src_node = f;
			break;
		}
	}

	if (!src_file) {
		ret = -ENOENT;
		goto _cleanup_src;
	}

	if (src_file->type != TMPFS_TYPE_DIR && endswith(src_name, '/')) {
		/* Source ended with trailing slashes, but was not a directory. */
		ret = -ENOTDIR;
		goto _cleanup_src;
	}

	if (sticky_check(src_dir, ds, src_file)) {
		ret = -EPERM;
		goto _cleanup_src;
	}

	struct tmpfs_dir * dd = (struct tmpfs_dir *)dest_dir->impl;
	if (dd != ds) spin_lock(dd->lock);

	struct tmpfs_file * dest_file = NULL;
	node_t * dest_node = NULL;
	foreach(f, dd->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!path_comp(dest_name, t->name)) {
			dest_file = t;
			dest_node = f;
			break;
		}
	}

	if (dest_file && dest_file->type != TMPFS_TYPE_DIR && endswith(dest_name, '/')) {
		/* Destination ended with trailing slashes, but was not a directory. */
		ret = -ENOTDIR;
		goto _cleanup;
	}

	/* Check that src_file isn't a parent of dest_file */
	if (src_file->type == TMPFS_TYPE_DIR) {
		struct tmpfs_dir * pd = dd;
		while (pd) {
			if ((void*)pd == (void*)src_file) {
				ret = -EINVAL;
				goto _cleanup;
			}
			pd = pd->parent;
		}
	}

	if (!dest_file) {
		if (endswith(dest_name,'/') && src_file->type != TMPFS_TYPE_DIR) {
			/* Destination did not exist, ended with trailing slashes, but the source was not a directory. */
			ret = -ENOTDIR;
			goto _cleanup;
		}
		char * old_name = src_file->name;
		src_file->name = path_dup(dest_name);
		free(old_name);

		list_insert(dd->files, src_file);
		list_delete(ds->files, src_node);
	} else if (src_file == dest_file) {
		/* Do nothing */
	} else {
		if (dest_file->type == TMPFS_TYPE_DIR) {
			struct tmpfs_dir * dest = (struct tmpfs_dir*)dest_file;
			if (dest->files && dest->files->length) {
				/* Destination is not empty */
				ret = -ENOTEMPTY;
				goto _cleanup;
			}
			if (src_file->type != TMPFS_TYPE_DIR) {
				/* Source is not a directory but destination is */
				ret = -EISDIR;
				goto _cleanup;
			}
		} else if (src_file->type == TMPFS_TYPE_DIR) {
			/* Source is a directory, but destination is not */
			ret = -ENOTDIR;
			goto _cleanup;
		}

		if (sticky_check(dest_dir, dd, dest_file)) {
			ret = -EPERM;
			goto _cleanup;
		}

		/* Rename src */
		char * old_name = src_file->name;
		src_file->name = path_dup(dest_name);
		free(old_name);

		list_delete(ds->files, src_node);
		dest_node->value = src_file;

		/* Unlink the original destination file */
		if (dest_file->type == TMPFS_TYPE_DIR) {
			try_free_dir((void*)dest_file);
		} else {
			tmpfs_file_free(dest_file);
		}
	}

_cleanup:
	if (dd != ds) spin_unlock(dd->lock);
_cleanup_src:
	spin_unlock(ds->lock);
	spin_unlock(root->nest_lock);
	return ret;
}

static ssize_t get_size_tmpfsdir(fs_node_t * node) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)node->impl;
	return sizeof(struct dirent) * (d->files->length + 2);
}

static fs_vtable_t tmpfs_dir_ops = {
	.readdir = readdir_tmpfs,
	.finddir = finddir_tmpfs,
	.create  = create_tmpfs,
	.unlink  = unlink_tmpfs,
	.mkdir   = mkdir_tmpfs,
	.symlink = symlink_tmpfs,
	.get_size = get_size_tmpfsdir,
	.chown   = chown_tmpfs,
	.chmod   = chmod_tmpfs,
	.rename  = rename_tmpfs,
	.utimens = utimens_tmpfs,
};

static fs_node_t * tmpfs_from_dir(struct tmpfs_dir * d) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	spin_lock(d->lock);
	memset(fnode, 0x00, sizeof(fs_node_t));
	strcpy(fnode->name, "tmp");
	fnode->mount = d->mount;
	fnode->device = d->mount;
	fnode->mask = d->mask;
	fnode->uid  = d->uid;
	fnode->gid  = d->gid;
	fnode->impl    = (uintptr_t)d;
	fnode->inode   = d->ino;
	fnode->atime   = d->atime;
	fnode->mtime   = d->mtime;
	fnode->ctime   = d->ctime;
	fnode->flags   = FS_DIRECTORY;
	fnode->nlink   = 1; /* should be "number of children that are directories + 1" */
	fnode->ops     = &tmpfs_dir_ops;
	spin_unlock(d->lock);

	return fnode;
}

fs_node_t * tmpfs_create(char * name) {
	struct tmpfs_dir * tmpfs_root = tmpfs_dir_new(name, NULL);
	tmpfs_root->mask = 0777;
	tmpfs_root->uid  = 0;
	tmpfs_root->gid  = 0;

	fs_node_t * out = tmpfs_from_dir(tmpfs_root);
	tmpfs_root->mount = out;
	out->mount = out;
	out->device = out;
	return out;
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
			int mode = 0;
			for (unsigned int i = 0; i < strlen(argv[1]); ++i) {
				mode <<= 3;
				mode |= argv[1][i] - '0';
			}
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
	0
};

void tmpfs_register_init(void) {
	vfs_register("tmpfs", tmpfs_mount);
	procfs_install(&tmpfs_entry);
}

