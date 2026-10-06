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

static volatile intptr_t tmpfs_total_blocks = 0;
static volatile size_t   tmpfs_ino_counter = 1;

static fs_vtable_t tmpfs_file_ops;
static fs_vtable_t tmpfs_dir_ops;
static fs_vtable_t tmpfs_link_ops;

static struct tmpfs_file * tmpfs_file_new(fs_node_t * parent) {
	struct tmpfs_file * t = calloc(1, sizeof(struct tmpfs_file));
	spin_init(t->lock);
	t->pointers = 2;

	t->_node.mount = parent->mount;
	t->_node.device = parent->device;
	t->_node.refcount = 0; /* parent directory */
	t->_node.type  = INO_REG;
	t->_node.atime = now();
	t->_node.mtime = t->_node.atime;
	t->_node.ctime = t->_node.atime;
	t->_node.inode = tmpfs_ino_counter++;
	t->_node.ops = &tmpfs_file_ops;

	t->blocks = calloc(t->pointers, sizeof(char *));

	return t;
}

static struct tmpfs_dir * tmpfs_dir_new(struct tmpfs_dir * parent) {
	struct tmpfs_dir * d = calloc(1, sizeof(struct tmpfs_dir));
	spin_init(d->lock);
	spin_init(d->nest_lock);
	d->parent = parent;

	d->_node.mount = parent ? parent->_node.mount : (fs_node_t*)d;
	d->_node.device = parent ? parent->_node.device : (fs_node_t*)d;
	d->_node.refcount = 0;
	d->_node.type  = INO_DIR;
	d->_node.atime = now();
	d->_node.mtime = d->_node.atime;
	d->_node.ctime = d->_node.atime;
	d->_node.inode = tmpfs_ino_counter++;
	d->_node.ops = &tmpfs_dir_ops;

	d->files = list_create("tmpfs directory entries",d);
	return d;
}

struct tmpfs_dirent {
	struct tmpfs_file * inode;
	char * name;
};

static int path_comp(const char * a, const char * b) {
	while (*a && *b && *a != '/' && *b != '/') {
		if (*a != *b) return 1;
		a++;
		b++;
	}

	if ((*a == '/' || !*a) && (*b == '/' || !*b)) return 0;
	return 1;
}

static struct tmpfs_dirent * get_file(struct tmpfs_dir * d, const char * name, node_t ** node, int accept_slash) {
	foreach(f, d->files) {
		struct tmpfs_dirent * de = (struct tmpfs_dirent *)f->value;
		if (accept_slash) {
			if (!path_comp(name, de->name)) {
				if (node) *node = f;
				return de;
			}
		} else if (!strcmp(name, de->name)) {
			if (node) *node = f;
			return de;
		}
	}
	return NULL;
}

static void add_new_file(struct tmpfs_dir * d, const char * name, struct tmpfs_file * f) {
	struct tmpfs_dirent * ndent = calloc(1, sizeof(struct tmpfs_dirent));
	f->_node.refcount++;
	f->_node.nlink++;
	ndent->inode = f;
	ndent->name = strdup(name);
	list_insert(d->files, ndent);
}

static int symlink_tmpfs(fs_node_t * parent, const char * target, const char * name) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)parent;

	spin_lock(d->lock);
	struct tmpfs_dirent * de = get_file(d, name, NULL, 0);
	if (de) {
		spin_unlock(d->lock);
		return -EEXIST;
	}

	struct tmpfs_file * t = tmpfs_file_new(parent);
	t->_node.type = INO_LNK;
	t->_node.mask = 0777;
	t->_node.uid = this_core->current_process->user;
	t->_node.gid = this_core->current_process->user;
	t->_node.length = strlen(target);
	t->_node.ops = &tmpfs_link_ops;
	spin_lock(t->lock);

	t->target = strdup(target);

	add_new_file(d, name, t);
	spin_unlock(t->lock);
	spin_unlock(d->lock);

	return 0;
}

static ssize_t readlink_tmpfs(fs_node_t * node, char * buf, size_t size) {
	struct tmpfs_file * t = (struct tmpfs_file *)node;

	spin_lock(t->lock);
	if (t->_node.type != INO_LNK) {
		spin_unlock(t->lock);
		return -EINVAL;
	}

	size_t len = strlen(t->target);
	if (size < len) len = size;
	memcpy(buf, t->target, len);
	spin_unlock(t->lock);
	return len;
}

static void close_tmpfs(fs_node_t * node) {
	if (node->nlink != 0) {
		dprintf("tmpfs: unexpected close of file with links\n");
		return;
	}
	struct tmpfs_file * t = (void*)node;
	spin_lock(t->lock);
	if (t->_node.type == INO_LNK) {
		/* free target string */
		free(t->target);
		t->target = NULL;
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
	struct tmpfs_file * t = (struct tmpfs_file *)node;

	spin_lock(t->lock);

	t->_node.atime = now();

	if ((size_t)offset >= node->length) {
		spin_unlock(t->lock);
		return 0;
	}

	uint64_t end;
	if ((size_t)offset + size > node->length) {
		end = node->length;
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
	struct tmpfs_file * t = (struct tmpfs_file *)node;

	spin_lock(t->lock);
	node->mtime = node->atime = now();

	uint64_t end;
	if ((size_t)offset + size > node->length) {
		node->length = offset + size;
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
	node->mask = mode;
	return 0;
}

static int chown_tmpfs(fs_node_t * node, int uid, int gid) {
	struct tmpfs_file * t = (struct tmpfs_file *)node;

	spin_lock(t->lock);
	if (uid != -1) t->_node.uid = uid;
	if (gid != -1) t->_node.gid = gid;
	spin_unlock(t->lock);

	return 0;
}

static int utimens_tmpfs(fs_node_t * node, struct timespec access, struct timespec modify) {
	if (access.tv_nsec != UTIME_OMIT) {
		node->atime = access.tv_sec;
	}
	if (modify.tv_nsec != UTIME_OMIT) {
		node->mtime = modify.tv_sec;
	}
	return 0;
}


static int truncate_tmpfs(fs_node_t * node, size_t size) {
	struct tmpfs_file * t = (struct tmpfs_file *)node;
	spin_lock(t->lock);

	if (size == node->length) goto _exit_truncate;

	uint64_t old_end_block = (node->length / BLOCKSIZE);
	uint64_t old_end_size  = node->length - old_end_block * BLOCKSIZE;
	uint64_t old_blocks = old_end_block + !!old_end_size;
	uint64_t new_end_block = (size / BLOCKSIZE);
	uint64_t new_end_size  = size - new_end_block * BLOCKSIZE;
	uint64_t new_blocks = new_end_block + !!new_end_size;


	/* Is the target size bigger or smaller? */
	if (size > node->length) {
		if (old_end_block == new_end_block) {
			char *buf = tmpfs_file_getset_block(t, old_end_block, old_end_size ? 0 : 2);
			memset(buf + old_end_size, 0, new_end_size - old_end_size);
		} else {
			tmpfs_file_getset_block(t, new_end_block, 2);
			char *buf = tmpfs_file_getset_block(t, old_end_block, 0);
			memset(buf + old_end_size, 0, BLOCKSIZE - old_end_size);
		}
		node->length = size;
		goto _exit_truncate;
	}

	if (size == 0) {
		for (size_t i = 0; i < t->block_count; ++i) {
			mmu_frame_release((uintptr_t)t->blocks[i] * 0x1000);
			tmpfs_total_blocks--;
			t->blocks[i] = 0;
		}
		t->block_count = 0;
		node->length = 0;
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

	node->length = size;

_exit_truncate:
	node->mtime = node->atime;
	spin_unlock(t->lock);
	return 0;
}

static void open_tmpfs(fs_node_t * node, unsigned int flags) {
	node->atime = now();
}

static int fault_map_tmpfs(fs_node_t * node, union PML * page, off_t offset, int fault_flags, int map_flags, int prot, int *mmu_flags) {
	struct tmpfs_file * t = (struct tmpfs_file *)node;

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
	.fault_map = fault_map_tmpfs,
	.utimens = utimens_tmpfs,
	.close   = close_tmpfs,
};

static fs_vtable_t tmpfs_link_ops = {
	.readlink = readlink_tmpfs,
};

static int readdir_tmpfs(fs_node_t *node, uint64_t index, struct dirent * out) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)node;
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
			struct tmpfs_dirent * t = (struct tmpfs_dirent *)f->value;
			memset(out, 0x00, sizeof(struct dirent));
			out->d_ino = ((fs_node_t*)t->inode)->inode;
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

	struct tmpfs_dir * d = (struct tmpfs_dir *)node;
	spin_lock(d->lock);
	struct tmpfs_dirent * de = get_file(d, name, NULL, 0);
	spin_unlock(d->lock);
	if (!de) return NULL;
	return (fs_node_t*)de->inode;
}

static void close_tmpfs_dir(fs_node_t * node) {
	if (node->nlink != 0) {
		dprintf("tmpfs: unexpected close of directory with nlinks\n");
		return;
	}

	struct tmpfs_dir * d = (void*)node;
	if (d->files->length) {
		dprintf("tmpfs: unexpected close of directory with files?\n");
		return;
	}

	free(d->files);

	/* close itself frees the directory */
}

static int try_free_dir(struct tmpfs_dir * d) {
	spin_lock(d->lock);
	if (d->files && d->files->length != 0) {
		spin_unlock(d->lock);
		return 1;
	}
	d->_node.nlink--;
	spin_unlock(d->lock);
	close_fs((fs_node_t*)d);
	return 0;
}

static int sticky_check(fs_node_t * node, struct tmpfs_dir * d, struct tmpfs_file *t) {
	if (!(node->mask & S_ISVTX)) return 0;
	uid_t me = this_core->current_process->user;
	if (me == 0 || me == d->_node.uid || me == t->_node.uid) return 0;
	return 1;
}

static int unlink_tmpfs(fs_node_t * node, const char * name) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)node;

	spin_lock(d->lock);
	node_t * d_node = NULL;
	struct tmpfs_dirent * de = get_file(d, name, &d_node, 0);
	if (!de) {
		spin_unlock(d->lock);
		return -ENOENT;
	}

	if (sticky_check(node, d, de->inode)) {
		spin_unlock(d->lock);
		return -EPERM;
	}

	if (de->inode->_node.type == INO_DIR) {
		if (try_free_dir((void*)de->inode)) {
			spin_unlock(d->lock);
			return -ENOTEMPTY;
		}
	} else {
		de->inode->_node.nlink--;
		close_fs((void*)de->inode); /* Remove this reference */
	}

	free(de->name);
	free(de);
	list_delete(d->files, d_node);
	free(d_node);

	spin_unlock(d->lock);
	return 0;
}

static int create_tmpfs(fs_node_t *parent, const char *name, mode_t permission, fs_node_t ** out) {
	if (!name) return -EINVAL;

	struct tmpfs_dir * d = (struct tmpfs_dir *)parent;
	spin_lock(d->lock);

	struct tmpfs_dirent * de = get_file(d, name, NULL, 0);
	if (de) {
		spin_unlock(d->lock);
		return -EEXIST; /* Already exists */
	}

	struct tmpfs_file * t = tmpfs_file_new(parent);
	t->_node.mask = permission;
	t->_node.uid = this_core->current_process->user;
	t->_node.gid = this_core->current_process->user_group;

	add_new_file(d, name, t);
	*out = (fs_node_t*)t;
	spin_unlock(d->lock);

	return 0;
}

static int mkdir_tmpfs(fs_node_t * parent, const char * name, mode_t permission, fs_node_t ** out_node) {
	if (!name) return -EINVAL;
	if (!strlen(name)) return -EINVAL;

	struct tmpfs_dir * d = (struct tmpfs_dir *)parent;
	spin_lock(d->lock);

	struct tmpfs_dirent * de = get_file(d, name, NULL, 0);
	if (de) {
		spin_unlock(d->lock);
		return -EEXIST; /* Already exists */
	}

	/* Need both exec and write on the parent to create a new entry */
	if (!has_permission(parent, W_OK|X_OK)) {
		spin_unlock(d->lock);
		return -EACCES;
	}

	struct tmpfs_dir * out = tmpfs_dir_new(d);
	out->_node.mask = permission;
	out->_node.uid  = this_core->current_process->user;
	out->_node.gid  = this_core->current_process->user;

	add_new_file(d, name, (struct tmpfs_file*)out);

	if (out_node) *out_node = (fs_node_t*)out;
	spin_unlock(d->lock);

	return 0;
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

	struct tmpfs_dir * root = (struct tmpfs_dir*)mount_root;
	spin_lock(root->nest_lock);

	struct tmpfs_dir * ds = (struct tmpfs_dir *)src_dir;
	spin_lock(ds->lock);

	/* First, get the source file */
	node_t * src_node = NULL;
	struct tmpfs_file * src_file = NULL;
	struct tmpfs_dirent * src_dent = get_file(ds, src_name, &src_node, 1);

	if (!src_dent) {
		ret = -ENOENT;
		goto _cleanup_src;
	}

	src_file = src_dent->inode;

	if (src_file->_node.type != INO_DIR && endswith(src_name, '/')) {
		/* Source ended with trailing slashes, but was not a directory. */
		ret = -ENOTDIR;
		goto _cleanup_src;
	}

	if (sticky_check(src_dir, ds, src_file)) {
		ret = -EPERM;
		goto _cleanup_src;
	}

	struct tmpfs_dir * dd = (struct tmpfs_dir *)dest_dir;
	if (dd != ds) spin_lock(dd->lock);

	node_t * dest_node = NULL;
	struct tmpfs_file * dest_file = NULL;
	struct tmpfs_dirent * dest_dent = get_file(dd, dest_name, &dest_node, 1);
	if (dest_dent) dest_file = dest_dent->inode;

	if (dest_file && dest_file->_node.type != INO_DIR  && endswith(dest_name, '/')) {
		/* Destination ended with trailing slashes, but was not a directory. */
		ret = -ENOTDIR;
		goto _cleanup;
	}

	/* Check that src_file isn't a parent of dest_file */
	if (src_file->_node.type == INO_DIR) {
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
		if (endswith(dest_name,'/') && src_file->_node.type != INO_DIR) {
			/* Destination did not exist, ended with trailing slashes, but the source was not a directory. */
			ret = -ENOTDIR;
			goto _cleanup;
		}

		char * dest__name = path_dup(dest_name);
		add_new_file(dd, dest__name, src_file); /* Increments counter */
		free(dest__name);

		src_file->_node.nlink--;
		close_fs((void*)src_file);

		list_delete(ds->files, src_node);
		free(src_dent->name);
		free(src_dent);
		free(src_node);
	} else if (src_file == dest_file) {
		/* Do nothing */
	} else {
		if (dest_file->_node.type == INO_DIR) {
			struct tmpfs_dir * dest = (struct tmpfs_dir*)dest_file;
			if (dest->files && dest->files->length) {
				/* Destination is not empty */
				ret = -ENOTEMPTY;
				goto _cleanup;
			}
			if (src_file->_node.type != INO_DIR) {
				/* Source is not a directory but destination is */
				ret = -EISDIR;
				goto _cleanup;
			}
		} else if (src_file->_node.type == INO_DIR) {
			/* Source is a directory, but destination is not */
			ret = -ENOTDIR;
			goto _cleanup;
		}

		if (sticky_check(dest_dir, dd, dest_file)) {
			ret = -EPERM;
			goto _cleanup;
		}

		/* Point existing directory entry to new target */
		dest_dent->inode = src_file;

		/* Close old target to decrement refcount */
		dest_file->_node.nlink--;
		close_fs((void*)dest_file);

		list_delete(ds->files, src_node);
		free(src_dent->name);
		free(src_dent);
		free(src_node);
	}

_cleanup:
	if (dd != ds) spin_unlock(dd->lock);
_cleanup_src:
	spin_unlock(ds->lock);
	spin_unlock(root->nest_lock);
	return ret;
}

static int hardlink_tmpfs(struct fs_node * dirnode, const char * name, struct fs_node * target) {
	/* No hard linking directories because I said so. */
	if (target->type == INO_DIR) return -EPERM;
	/* Probably good enough, increases link and refcount, and
	 * VFS should have checked if file exists... TOCTOUs aside. */
	add_new_file((struct tmpfs_dir*)dirnode, name, (struct tmpfs_file*)target);
	return 0;
}

static ssize_t get_size_tmpfsdir(fs_node_t * node) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)node;
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
	.hardlink = hardlink_tmpfs,
	.close = close_tmpfs_dir,
	.can_mount = 1,
};

fs_node_t * tmpfs_create(void) {
	struct tmpfs_dir * tmpfs_root = tmpfs_dir_new(NULL);
	tmpfs_root->_node.mask = 0777;
	return (fs_node_t*)tmpfs_root;
}

fs_node_t * tmpfs_mount(const char * device, const char * mount_path) {
	char * arg = strdup(device);
	char * argv[10];
	int argc = tokenize(arg, ",", argv);

	fs_node_t * fs = tmpfs_create();
	fs->refcount = 1;

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

