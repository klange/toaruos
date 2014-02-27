/*
 * Local Toaru Socket Filesystem
 *
 * Based on tmpfs, but doesn't support actual files.
 * Directories are "namespaces" of sockets, files are
 * the socket endpoints (ports). To create a server
 * socket, creat() the respective endpoint. To open
 * a connection, just open the file. You can also
 * create new namespaces with mkdir.
 */
#include <system.h>
#include <logging.h>
#include <fs.h>
#include <version.h>
#include <process.h>

#define SOCKFS_TYPE_SOCK 1
#define SOCKFS_TYPE_NMSP  2

static uint8_t volatile lock = 0;

struct sockfs_file {
	char * name;
	int    type;
	int    mask;
	int    uid;
	int    gid;
	unsigned int atime;
	unsigned int mtime;
	unsigned int ctime;
	fs_node_t * pipe;
};

struct sockfs_dir;

struct sockfs_dir {
	char * name;
	int    type;
	int    mask;
	int    uid;
	int    gid;
	unsigned int atime;
	unsigned int mtime;
	unsigned int ctime;
	list_t * files;
	struct sockfs_dir * parent;
};

struct sockfs_dir * sockfs_root = NULL;

fs_node_t * sockfs_from_dir(struct sockfs_dir * d);

static struct sockfs_file * sockfs_file_new(char * name) {

	spin_lock(&lock);

	struct sockfs_file * t = malloc(sizeof(struct sockfs_file));
	t->name = strdup(name);
	t->type = SOCKFS_TYPE_SOCK;
	t->mask = 0;
	t->uid = 0;
	t->gid = 0;
	t->atime = now();
	t->mtime = t->atime;
	t->ctime = t->atime;

	spin_unlock(&lock);
	return t;
}

static struct sockfs_dir * sockfs_dir_new(char * name, struct sockfs_dir * parent) {
	spin_lock(&lock);

	struct sockfs_dir * d = malloc(sizeof(struct sockfs_dir));
	d->name = strdup(name);
	d->type = SOCKFS_TYPE_NMSP;
	d->mask = 0;
	d->uid = 0;
	d->gid = 0;
	d->atime = now();
	d->mtime = d->atime;
	d->ctime = d->atime;
	d->files = list_create();

	spin_unlock(&lock);
	return d;
}

static uint32_t read_sockfs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	struct sockfs_file * t = (struct sockfs_file *)(node->device);

	t->atime = now();

	return 0;
}

static uint32_t write_sockfs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	struct sockfs_file * t = (struct sockfs_file *)(node->device);

	t->atime = now();
	t->mtime = t->atime;

	return 0;
}

static int chmod_sockfs(fs_node_t * node, int mode) {
	struct sockfs_file * t = (struct sockfs_file *)(node->device);

	/* XXX permissions */
	t->mask = mode;

	return 0;
}

static void open_sockfs(fs_node_t * node, unsigned int flags) {
	struct sockfs_file * t = (struct sockfs_file *)(node->device);

	debug_print(WARNING, "---- Opened sockfs file %s with flags 0x%x ----", t->name, flags);

	if (flags & O_TRUNC) {
		debug_print(WARNING, "Truncating file %s", t->name);
	}

	return;
}

static void sockfs_sock_dispose(struct sockfs_file * f) {
	/* XXX */
}

static fs_node_t * sockfs_from_file(struct sockfs_file * t) {
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
	fnode->read    = read_sockfs;
	fnode->write   = write_sockfs;
	fnode->open    = open_sockfs;
	fnode->close   = NULL;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->chmod   = chmod_sockfs;
	fnode->length  = 0;
	return fnode;
}

static struct dirent * readdir_sockfs(fs_node_t *node, uint32_t index) {
	struct sockfs_dir * d = (struct sockfs_dir *)node->device;
	uint32_t i = 0;

	debug_print(NOTICE, "sockfs - readdir id=%d", index);

	if (index >= d->files->length) return NULL;

	foreach(f, d->files) {
		if (i == index) {
			struct sockfs_file * t = (struct sockfs_file *)f->value;
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

static fs_node_t * finddir_sockfs(fs_node_t * node, char * name) {
	if (!name) return NULL;

	struct sockfs_dir * d = (struct sockfs_dir *)node->device;

	spin_lock(&lock);

	foreach(f, d->files) {
		struct sockfs_file * t = (struct sockfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(&lock);
			switch (t->type) {
				case SOCKFS_TYPE_SOCK:
					return sockfs_from_file(t);
				case SOCKFS_TYPE_NMSP:
					return sockfs_from_dir((struct sockfs_dir *)t);
			}
		}
	}

	spin_unlock(&lock);

	return NULL;
}

static void unlink_sockfs(fs_node_t * node, char * name) {
	struct sockfs_dir * d = (struct sockfs_dir *)node->device;
	int i = -1, j = 0;
	spin_lock(&lock);

	foreach(f, d->files) {
		struct sockfs_file * t = (struct sockfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			sockfs_sock_dispose(t);
			free(t);
			i = j;
			break;
		}
		j++;
	}

	if (i >= 0) {
		list_remove(d->files, i);
	}

	spin_unlock(&lock);
	return;
}

void create_sockfs(fs_node_t *parent, char *name, uint16_t permission) {
	if (!name) return;

	struct sockfs_dir * d = (struct sockfs_dir *)parent->device;
	debug_print(CRITICAL, "Creating sockfs file %s in %s", name, d->name);

	spin_lock(&lock);
	foreach(f, d->files) {
		struct sockfs_file * t = (struct sockfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(&lock);
			debug_print(WARNING, "... already exists.");
			return; /* Already exists */
		}
	}
	spin_unlock(&lock);

	debug_print(NOTICE, "... creating a new file.");
	struct sockfs_file * t = sockfs_file_new(name);
	t->mask = permission;
	t->uid = current_process->user;
	t->gid = current_process->user;

	list_insert(d->files, t);
}

void mkdir_sockfs(fs_node_t * parent, char * name, uint16_t permission) {
	if (!name) return;

	struct sockfs_dir * d = (struct sockfs_dir *)parent->device;
	debug_print(CRITICAL, "Creating sockfs directory %s (in %s)", name, d->name);

	spin_lock(&lock);
	foreach(f, d->files) {
		struct sockfs_file * t = (struct sockfs_file *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(&lock);
			debug_print(WARNING, "... already exists.");
			return; /* Already exists */
		}
	}
	spin_unlock(&lock);

	debug_print(NOTICE, "... creating a new directory.");
	struct sockfs_dir * out = sockfs_dir_new(name, d);
	out->mask = permission;
	out->uid  = current_process->user;
	out->gid  = current_process->user;

	list_insert(d->files, out);
}

fs_node_t * sockfs_from_dir(struct sockfs_dir * d) {
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
	fnode->readdir = readdir_sockfs;
	fnode->finddir = finddir_sockfs;
	fnode->create  = create_sockfs;
	fnode->unlink  = unlink_sockfs;
	fnode->mkdir   = mkdir_sockfs;

	return fnode;
}

fs_node_t * sockfs_create(void) {
	sockfs_root = sockfs_dir_new("tmp", NULL);
	sockfs_root->mask = 0777;
	sockfs_root->uid  = 0;
	sockfs_root->gid  = 0;

	return sockfs_from_dir(sockfs_root);
}
