#include <system.h>
#include <logging.h>
#include <fs.h>
#include <version.h>
#include <process.h>

/* 1KB */
#define BLOCKSIZE 1024

static uint8_t volatile lock = 0;

struct tmpfs_file {
	char * name;
	size_t length;
	size_t block_count;
	size_t pointers;
	uint32_t flags;
	char ** blocks;
};

list_t * tmpfs_files = NULL;

char empty_block[BLOCKSIZE] = {0};

static struct tmpfs_file * tmpfs_file_new(char * name) {

	spin_lock(&lock);

	struct tmpfs_file * t = malloc(sizeof(struct tmpfs_file));
	t->name = strdup(name);
	t->length = 0;
	t->pointers = 2;
	t->block_count = 0;
	t->flags = 0;
	t->blocks = malloc(t->pointers * sizeof(char *));
	for (size_t i = 0; i < t->pointers; ++i) {
		t->blocks[i] = NULL;
	}

	spin_unlock(&lock);
	return t;
}

static void tmpfs_file_blocks_embiggen(struct tmpfs_file * t) {
	t->pointers *= 2;
	debug_print(INFO, "Embiggening file %s to %d blocks", t->name, t->pointers);
	t->blocks = realloc(t->blocks, sizeof(char *) * t->pointers);
}

static char * tmpfs_file_getset_block(struct tmpfs_file * t, size_t blockid, int create) {
	debug_print(INFO, "Reading block %d from file %s", blockid, t->name);
	if (create) {
		spin_lock(&lock);
		while (blockid >= t->pointers) {
			tmpfs_file_blocks_embiggen(t);
		}
		while (blockid >= t->block_count) {
			debug_print(INFO, "Allocating block %d for file %s", blockid, t->name);
			t->blocks[t->block_count] = malloc(BLOCKSIZE);
			t->block_count += 1;
		}
		spin_unlock(&lock);
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
	if (end_size == 0) {
		end_block--;
	}
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
		void *buf = tmpfs_file_getset_block(t, end_block, 0);
		memcpy(buffer + BLOCKSIZE * blocks_read - (offset % BLOCKSIZE), buf, end_size);
	}
	return size_to_read;
}

static uint32_t write_tmpfs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	struct tmpfs_file * t = (struct tmpfs_file *)(node->device);
	uint32_t end;
	if (offset + size > t->length) {
		t->length = offset + size;
	}
	end = offset + size;
	uint32_t start_block  = offset / BLOCKSIZE;
	uint32_t end_block    = end / BLOCKSIZE;
	uint32_t end_size     = end - end_block * BLOCKSIZE;
	uint32_t size_to_read = end - offset;
	if (end_size == 0) {
		end_block--;
	}
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
		void *buf = tmpfs_file_getset_block(t, end_block, 1);
		memcpy(buf, buffer + BLOCKSIZE * blocks_read - (offset % BLOCKSIZE), end_size);
	}
	return size_to_read;
}

static fs_node_t * tmpfs_from_file(struct tmpfs_file * t) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, t->name);
	fnode->device = t;
	fnode->mask = 0777;
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->flags   = FS_FILE;
	fnode->read    = read_tmpfs;
	fnode->write   = write_tmpfs;
	fnode->open    = NULL;
	fnode->close   = NULL;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->length  = t->length;
	return fnode;
}

static struct dirent * readdir_tmpfs(fs_node_t *node, uint32_t index) {
	uint32_t i = 0;

	debug_print(NOTICE, "tmpfs - readdir id=%d", index);

	if (index >= tmpfs_files->length) return NULL;

	foreach(f, tmpfs_files) {
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

	spin_lock(&lock);

	foreach(f, tmpfs_files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(&lock);
			return tmpfs_from_file(t);
		}
	}

	spin_unlock(&lock);

	return NULL;
}

void create_tmpfs(fs_node_t *parent, char *name, uint16_t permission) {
	if (!name) return;

	debug_print(CRITICAL, "Creating TMPFS file %s", name);

	spin_lock(&lock);
	foreach(f, tmpfs_files) {
		struct tmpfs_file * t = (struct tmpfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(&lock);
			debug_print(WARNING, "... already exists.");
			return; /* Already exists */
		}
	}
	spin_unlock(&lock);

	debug_print(NOTICE, "... creating a new file.");
	struct tmpfs_file * t = tmpfs_file_new(name);
	t->flags = permission;

	list_insert(tmpfs_files, t);
}

fs_node_t * tmpfs_create() {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "tmp");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->flags   = FS_DIRECTORY;
	fnode->read    = NULL;
	fnode->write   = NULL;
	fnode->open    = NULL;
	fnode->close   = NULL;
	fnode->readdir = readdir_tmpfs;
	fnode->finddir = finddir_tmpfs;
	fnode->create  = create_tmpfs;

	tmpfs_files = list_create();
	return fnode;
}
