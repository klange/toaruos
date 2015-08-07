/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <logging.h>
#include <fs.h>
#include <version.h>
#include <process.h>

#include <module.h>
#include <mod/tmpfs.h>

/* 1KB */
#define BLOCKSIZE 1024

#define TMPFS_TYPE_FILE 1
#define TMPFS_TYPE_DIR  2

static spin_lock_t tmpfs_lock = { 0 };

struct tmpfs_dir * tmpfs_root = NULL;

static fs_node_t * tmpfs_from_dir(struct tmpfs_dir * d);

static struct tmpfs_file * tmpfs_file_new(char * name) {

	spin_lock(tmpfs_lock);

	struct tmpfs_file * t = malloc(sizeof(struct tmpfs_file));
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
		t->blocks[i] = NULL;
	}

	spin_unlock(tmpfs_lock);
	return t;
}

static struct tmpfs_dir * tmpfs_dir_new(char * name, struct tmpfs_dir * parent) {
	spin_lock(tmpfs_lock);

	struct tmpfs_dir * d = malloc(sizeof(struct tmpfs_dir));
	d->name = strdup(name);
	d->type = TMPFS_TYPE_DIR;
	d->mask = 0;
	d->uid = 0;
	d->gid = 0;
	d->atime = now();
	d->mtime = d->atime;
	d->ctime = d->atime;
	d->files = list_create();

	spin_unlock(tmpfs_lock);
	return d;
}

static void tmpfs_file_free(struct tmpfs_file * t) {
	for (size_t i = 0; i < t->block_count; ++i) {
		free(t->blocks[i]);
	}
}

static void tmpfs_file_blocks_embiggen(struct tmpfs_file * t) {
	t->pointers *= 2;
	debug_print(INFO, "Embiggening file %s to %d blocks", t->name, t->pointers);
	t->blocks = realloc(t->blocks, sizeof(char *) * t->pointers);
}

static char * tmpfs_file_getset_block(struct tmpfs_file * t, size_t blockid, int create) {
	debug_print(INFO, "Reading block %d from file %s", blockid, t->name);
	if (create) {
		spin_lock(tmpfs_lock);
		while (blockid >= t->pointers) {
			tmpfs_file_blocks_embiggen(t);
		}
		while (blockid >= t->block_count) {
			debug_print(INFO, "Allocating block %d for file %s", blockid, t->name);
			t->blocks[t->block_count] = malloc(BLOCKSIZE);
			t->block_count += 1;
		}
		spin_unlock(tmpfs_lock);
	} else {
		if (blockid >= t->block_count) {
			debug_print(CRITICAL, "This will probably end badly.");
			return NULL;
		}
	}
	debug_print(WARNING, "Using block %d->0x%x (of %d) on file %s", blockid, t->blocks[blockid], t->block_count, t->name);
	return t->blocks[blockid];
}


static uint32_t read_tmpfs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);

	t->atime = now();

	uint32_t end;
	if (offset + size > t->length) {
		end = t->length;
	} else {
		end = offset + size;
	}
	debug_print(INFO, "reading from %d to %d", offset, end);
	uint32_t start_block  = offset / BLOCKSIZE;
	uint32_t end_block    = end / BLOCKSIZE;
	uint32_t end_size     = end - end_block * BLOCKSIZE;
	uint32_t size_to_read = end - offset;
	if (start_block == end_block && offset == end) return 0;
	if (start_block == end_block) {
		void *buf = tmpfs_file_getset_block(t, start_block, 0);
		memcpy(buffer, (uint8_t *)(((uint32_t)buf) + (offset % BLOCKSIZE)), size_to_read);
		return size_to_read;
	} else {
		uint32_t block_offset;
		uint32_t blocks_read = 0;
		for (block_offset = start_block; block_offset < end_block; block_offset++, blocks_read++) {
			if (block_offset == start_block) {
				void *buf = tmpfs_file_getset_block(t, block_offset, 0);
				memcpy(buffer, (uint8_t *)(((uint32_t)buf) + (offset % BLOCKSIZE)), BLOCKSIZE - (offset % BLOCKSIZE));
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
	return size_to_read;
}

static uint32_t write_tmpfs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);

	t->atime = now();
	t->mtime = t->atime;

	uint32_t end;
	if (offset + size > t->length) {
		t->length = offset + size;
	}
	end = offset + size;
	uint32_t start_block  = offset / BLOCKSIZE;
	uint32_t end_block    = end / BLOCKSIZE;
	uint32_t end_size     = end - end_block * BLOCKSIZE;
	uint32_t size_to_read = end - offset;
	if (start_block == end_block) {
		void *buf = tmpfs_file_getset_block(t, start_block, 1);
		memcpy((uint8_t *)(((uint32_t)buf) + (offset % BLOCKSIZE)), buffer, size_to_read);
		return size_to_read;
	} else {
		uint32_t block_offset;
		uint32_t blocks_read = 0;
		for (block_offset = start_block; block_offset < end_block; block_offset++, blocks_read++) {
			if (block_offset == start_block) {
				void *buf = tmpfs_file_getset_block(t, block_offset, 1);
				memcpy((uint8_t *)(((uint32_t)buf) + (offset % BLOCKSIZE)), buffer, BLOCKSIZE - (offset % BLOCKSIZE));
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
	return size_to_read;
}

static int chmod_tmpfs(fs_node_t * node, int mode) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);

	/* XXX permissions */
	t->mask = mode;

	return 0;
}

static void open_tmpfs(fs_node_t * node, unsigned int flags) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);

	debug_print(WARNING, "---- Opened TMPFS file %s with flags 0x%x ----", t->name, flags);

	if (flags & O_TRUNC) {
		debug_print(WARNING, "Truncating file %s", t->name);
		for (size_t i = 0; i < t->block_count; ++i) {
			free(t->blocks[i]);
			t->blocks[i] = 0;
		}
		t->block_count = 0;
		t->length = 0;
	}

	return;
}

static fs_node_t * tmpfs_from_file(struct tmpfs_file * t) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
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
	fnode->length  = t->length;
	fnode->nlink   = 1;
	return fnode;
}

static struct dirent * readdir_tmpfs(fs_node_t *node, uint32_t index) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)node->device;
	uint32_t i = 0;

	debug_print(NOTICE, "tmpfs - readdir id=%d", index);

	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, "..");
		return out;
	}

	index -= 2;

	if (index >= d->files->length) return NULL;

	foreach(f, d->files) {
		if (i == index) {
			struct tmpfs_file * t = (struct tmpfs_file *)f->value;
			struct dirent * out = malloc(sizeof(struct dirent));
			memset(out, 0x00, sizeof(struct dirent));
			out->ino = (uint32_t)t;
			strcpy(out->name, t->name);
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

	spin_lock(tmpfs_lock);

	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(tmpfs_lock);
			switch (t->type) {
				case TMPFS_TYPE_FILE:
					return tmpfs_from_file(t);
				case TMPFS_TYPE_DIR:
					return tmpfs_from_dir((struct tmpfs_dir *)t);
			}
			return NULL;
		}
	}

	spin_unlock(tmpfs_lock);

	return NULL;
}

static void unlink_tmpfs(fs_node_t * node, char * name) {
	struct tmpfs_dir * d = (struct tmpfs_dir *)node->device;
	int i = -1, j = 0;
	spin_lock(tmpfs_lock);

	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			tmpfs_file_free(t);
			free(t);
			i = j;
			break;
		}
		j++;
	}

	if (i >= 0) {
		list_remove(d->files, i);
	}

	spin_unlock(tmpfs_lock);
	return;
}

static void create_tmpfs(fs_node_t *parent, char *name, uint16_t permission) {
	if (!name) return;

	struct tmpfs_dir * d = (struct tmpfs_dir *)parent->device;
	debug_print(CRITICAL, "Creating TMPFS file %s in %s", name, d->name);

	spin_lock(tmpfs_lock);
	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(tmpfs_lock);
			debug_print(WARNING, "... already exists.");
			return; /* Already exists */
		}
	}
	spin_unlock(tmpfs_lock);

	debug_print(NOTICE, "... creating a new file.");
	struct tmpfs_file * t = tmpfs_file_new(name);
	t->mask = permission;
	t->uid = current_process->user;
	t->gid = current_process->user;

	spin_lock(tmpfs_lock);
	list_insert(d->files, t);
	spin_unlock(tmpfs_lock);
}

static void mkdir_tmpfs(fs_node_t * parent, char * name, uint16_t permission) {
	if (!name) return;

	struct tmpfs_dir * d = (struct tmpfs_dir *)parent->device;
	debug_print(CRITICAL, "Creating TMPFS directory %s (in %s)", name, d->name);

	spin_lock(tmpfs_lock);
	foreach(f, d->files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(tmpfs_lock);
			debug_print(WARNING, "... already exists.");
			return; /* Already exists */
		}
	}
	spin_unlock(tmpfs_lock);

	debug_print(NOTICE, "... creating a new directory.");
	struct tmpfs_dir * out = tmpfs_dir_new(name, d);
	out->mask = permission;
	out->uid  = current_process->user;
	out->gid  = current_process->user;

	spin_lock(tmpfs_lock);
	list_insert(d->files, out);
	spin_unlock(tmpfs_lock);
}

static fs_node_t * tmpfs_from_dir(struct tmpfs_dir * d) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
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

	return fnode;
}

fs_node_t * tmpfs_create(char * name) {
	tmpfs_root = tmpfs_dir_new(name, NULL);
	tmpfs_root->mask = 0777;
	tmpfs_root->uid  = 0;
	tmpfs_root->gid  = 0;

	return tmpfs_from_dir(tmpfs_root);
}

fs_node_t * tmpfs_mount(char * device, char * mount_path) {
	fs_node_t * fs = tmpfs_create(device);
	return fs;
}

static int tmpfs_initialize(void) {

	vfs_mount("/tmp", tmpfs_create("tmp"));
	vfs_mount("/var", tmpfs_create("var"));

	vfs_register("tmpfs", tmpfs_mount);

	return 0;
}
static int tmpfs_finalize(void) {
	return 0;
}

MODULE_DEF(tmpfs, tmpfs_initialize, tmpfs_finalize);
