/**
 * @file  kernel/vfs/vfs.c
 * @brief Virtual file system.
 *
 * Provides the high-level generic operations for the VFS.
 *
 * @warning Here be dragons
 *
 * This VFS implementation comes from toaru32. It has a lot of weird
 * quirks and doesn't quite work like a typical Unix VFS would.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2021 K. Lange
 * Copyright (C) 2014 Lioncash
 * Copyright (C) 2012 Tianyi Wang
 */
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <kernel/time.h>
#include <kernel/process.h>

#include <kernel/list.h>
#include <kernel/hashmap.h>
#include <kernel/tree.h>
#include <kernel/spinlock.h>

#define MAX_SYMLINK_DEPTH 8
#define MAX_SYMLINK_SIZE 4096

tree_t    * fs_tree = NULL; /* File system mountpoint tree */
fs_node_t * fs_root = NULL; /* Pointer to the root mount fs_node (must be some form of filesystem, even ramdisk) */

hashmap_t * fs_types = NULL;

#define MIN(l,r) ((l) < (r) ? (l) : (r))
#define MAX(l,r) ((l) > (r) ? (l) : (r))

#define debug_print(x, ...) do { if (0) {printf("vfs.c [%s] ", #x); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int cb_printf(void * user, char c) {
	fs_node_t * f = user;
	write_fs(f, 0, 1, (uint8_t*)&c);
	return 0;
}

/**
 * @brief Write printf output to a simple file node.
 *
 * The file node, f, must be a simple character device that
 * allows repeated writes of a single byte without an incrementing
 * offset, such as a serial port or TTY.
 */
int fprintf(fs_node_t * f, const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int out = xvasprintf(cb_printf, f, fmt, args);
	va_end(args);
	return out;
}

int has_permission(fs_node_t * node, int permission_bit) {
	if (!node) return 0;

	if (this_core->current_process->user == 0 && permission_bit != 01) { /* even root needs exec to exec */
		return 1;
	}

	uint64_t permissions = node->mask;

	uint8_t my_permissions = (permissions) & 07;
	uint8_t user_perm  = (permissions >> 6) & 07;
	uint8_t group_perm = (permissions >> 3) & 07;

	if (this_core->current_process->user == node->uid) my_permissions |= user_perm;
	if (this_core->current_process->user_group == node->gid) my_permissions |= group_perm;
	else if (this_core->current_process->supplementary_group_count) {
		for (int i = 0; i < this_core->current_process->supplementary_group_count; ++i) {
			if (this_core->current_process->supplementary_group_list[i] == node->gid) {
				my_permissions |= group_perm;
			}
		}
	}

	return (permission_bit & my_permissions);
}

static struct dirent * readdir_mapper(fs_node_t *node, unsigned long index) {
	tree_node_t * d = (tree_node_t *)node->device;

	if (!d) return NULL;

	if (index == 0) {
		struct dirent * dir = malloc(sizeof(struct dirent));
		strcpy(dir->d_name, ".");
		dir->d_ino = 0;
		return dir;
	} else if (index == 1) {
		struct dirent * dir = malloc(sizeof(struct dirent));
		strcpy(dir->d_name, "..");
		dir->d_ino = 1;
		return dir;
	}

	index -= 2;
	unsigned long i = 0;
	foreach(child, d->children) {
		if (i == index) {
			/* Recursively print the children */
			tree_node_t * tchild = (tree_node_t *)child->value;
			struct vfs_entry * n = (struct vfs_entry *)tchild->value;
			struct dirent * dir = malloc(sizeof(struct dirent));

			size_t len = strlen(n->name) + 1;
			memcpy(&dir->d_name, n->name, MIN(256, len));
			dir->d_ino = i;
			return dir;
		}
		++i;
	}

	return NULL;
}

static fs_node_t * vfs_mapper(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->mask = 0555;
	fnode->flags   = FS_DIRECTORY;
	fnode->readdir = readdir_mapper;
	fnode->ctime   = now();
	fnode->mtime   = now();
	fnode->atime   = now();
	return fnode;
}

/**
 * @brief Check if a read from this file would block.
 */
int selectcheck_fs(fs_node_t * node) {
	if (!node) return -ENOENT;

	if (node->selectcheck) {
		return node->selectcheck(node);
	}

	return -EINVAL;
}

/**
 * @brief Inform a node that it should alert the current_process.
 */
int selectwait_fs(fs_node_t * node, void * process) {
	if (!node) return -ENOENT;

	if (node->selectwait) {
		return node->selectwait(node, process);
	}

	return -EINVAL;
}

/**
 * @brief Read a file system node based on its underlying type.
 *
 * @param node    Node to read
 * @param offset  Offset into the node data to read from
 * @param size    How much data to read (in bytes)
 * @param buffer  A buffer to copy of the read data into
 * @returns Bytes read
 */
ssize_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	if (!node) return -ENOENT;
	if (node->read) {
		return node->read(node, offset, size, buffer);
	} else {
		return -EINVAL;
	}
}

/**
 * @brief Write a file system node based on its underlying type.
 *
 * @param node    Node to write to
 * @param offset  Offset into the node data to write to
 * @param size    How much data to write (in bytes)
 * @param buffer  A buffer to copy from
 * @returns Bytes written
 */
ssize_t write_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	if (!node) return -ENOENT;
	if (node->write) {
		return node->write(node, offset, size, buffer);
	} else {
		return -EROFS;
	}
}

/**
 * @brief set the size of a file to 9
 *
 * @param node File to resize
 */
int truncate_fs(fs_node_t * node) {
	if (!node) return -ENOENT;

	if (node->truncate) {
		return node->truncate(node);
	}

	return -EINVAL;
}

//volatile uint8_t tmp_refcount_lock = 0;
static spin_lock_t tmp_refcount_lock = { 0 };

void vfs_lock(fs_node_t * node) {
	spin_lock(tmp_refcount_lock);
	node->refcount = -1;
	spin_unlock(tmp_refcount_lock);
}

/**
 * @brief Open a file system node.
 *
 * @param node  Node to open
 * @param flags Same as open, specifies read/write/append/truncate
 */
void open_fs(fs_node_t *node, unsigned int flags) {

	if (!node) return;

	if (node->refcount >= 0) {
		spin_lock(tmp_refcount_lock);
		node->refcount++;
		spin_unlock(tmp_refcount_lock);
	}

	if (node->open) {
		node->open(node, flags);
	}
}

/**
 * @brief Close a file system node
 *
 * @param node Node to close
 */
void close_fs(fs_node_t *node) {
	//assert(node != fs_root && "Attempted to close the filesystem root. kablooey");

	if (!node) {
		debug_print(WARNING, "Double close? This isn't an fs_node.");
		return;
	}

	if (node->refcount == -1) return;

	spin_lock(tmp_refcount_lock);
	node->refcount--;
	if (node->refcount == 0) {
		debug_print(NOTICE, "Node refcount [%s] is now 0: %ld", node->name, node->refcount);

		if (node->close) {
			node->close(node);
		}

		free(node);
	}
	spin_unlock(tmp_refcount_lock);
}

/**
 * @brief Change permissions for a file system node.
 *
 * @param node Node to change permissions for
 * @param mode New mode bits
 */
int chmod_fs(fs_node_t *node, mode_t mode) {
	if (node->chmod) {
		return node->chmod(node, mode);
	}
	return 0;
}

/**
 * @brief Change ownership for a file system node.
 */
int chown_fs(fs_node_t *node, uid_t uid, gid_t gid) {
	if (node->chown) {
		return node->chown(node, uid, gid);
	}
	return 0;
}

/**
 * @brief Read a directory for the requested index
 *
 * @param node  Directory to read
 * @param index Offset to look for
 * @returns A dirent object.
 */
struct dirent *readdir_fs(fs_node_t *node, unsigned long index) {
	if (!node) return NULL;

	if ((node->flags & FS_DIRECTORY) && node->readdir) {
		return node->readdir(node, index);
	} else {
		return NULL;
	}
}

/**
 * @brief Find the requested file in the directory and return an fs_node for it
 *
 * @param node Directory to search
 * @param name File to look for
 * @returns An fs_node that the caller can free
 */
fs_node_t *finddir_fs(fs_node_t *node, char *name) {
	if (!node) return NULL;

	if ((node->flags & FS_DIRECTORY) && node->finddir) {
		return node->finddir(node, name);
	} else {
		debug_print(WARNING, "Node passed to finddir_fs isn't a directory!");
		debug_print(WARNING, "node = %p, name = %s", (void*)node, name);
		return NULL;
	}
}

/**
 * @brief Control Device
 *
 * @param node    Device node to control
 * @param request Device-specific request code
 * @param argp    Depends on `request`
 * @returns Depends on `request`
 */
int ioctl_fs(fs_node_t *node, unsigned long request, void * argp) {
	if (!node) return -ENOENT;

	if (node->ioctl) {
		return node->ioctl(node, request, argp);
	} else {
		return -EINVAL;
	}
}


/*
 * XXX: The following two function should be replaced with
 *      one function to create children of directory nodes.
 *      There is no fundamental difference between a directory
 *      and a file, thus, the use of flag sets should suffice
 */

int create_file_fs(char *name, mode_t permission) {
	fs_node_t * parent;
	char *cwd = (char *)(this_core->current_process->wd_name);
	char *path = canonicalize_path(cwd, name);

	char * parent_path = malloc(strlen(path) + 5);
	snprintf(parent_path, strlen(path) + 4, "%s/..", path);

	char * f_path = path + strlen(path) - 1;
	while (f_path > path) {
		if (*f_path == '/') {
			f_path += 1;
			break;
		}
		f_path--;
	}

	while (*f_path == '/') {
		f_path++;
	}

	debug_print(NOTICE, "creating file %s within %s (hope these strings are good)", f_path, parent_path);

	parent = kopen(parent_path, 0);
	free(parent_path);

	if (!parent) {
		debug_print(WARNING, "failed to open parent");
		free(path);
		return -ENOENT;
	}

	if (!has_permission(parent, 02)) {
		debug_print(WARNING, "bad permissions");
		return -EACCES;
	}

	int ret = 0;
	if (parent->create) {
		ret = parent->create(parent, f_path, permission);
	} else {
		ret = -EINVAL;
	}

	free(path);
	free(parent);

	return ret;
}

int unlink_fs(char * name) {
	fs_node_t * parent;
	char *cwd = (char *)(this_core->current_process->wd_name);
	char *path = canonicalize_path(cwd, name);

	char * parent_path = malloc(strlen(path) + 5);
	snprintf(parent_path, strlen(path) + 4, "%s/..", path);

	char * f_path = path + strlen(path) - 1;
	while (f_path > path) {
		if (*f_path == '/') {
			f_path += 1;
			break;
		}
		f_path--;
	}

	while (*f_path == '/') {
		f_path++;
	}

	debug_print(WARNING, "unlinking file %s within %s (hope these strings are good)", f_path, parent_path);

	parent = kopen(parent_path, 0);
	free(parent_path);

	if (!parent) {
		free(path);
		return -ENOENT;
	}

	if (!has_permission(parent, 02)) {
		free(path);
		close_fs(parent);
		return -EACCES;
	}

	int ret = 0;
	if (parent->unlink) {
		ret = parent->unlink(parent, f_path);
	} else {
		ret = -EINVAL;
	}

	free(path);
	close_fs(parent);
	return ret;
}

int mkdir_fs(char *name, mode_t permission) {
	fs_node_t * parent;
	char *cwd = (char *)(this_core->current_process->wd_name);
	char *path = canonicalize_path(cwd, name);

	if (!name || !strlen(name)) {
		return -EINVAL;
	}

	char * parent_path = malloc(strlen(path) + 5);
	snprintf(parent_path, strlen(path) + 4, "%s/..", path);

	char * f_path = path + strlen(path) - 1;
	while (f_path > path) {
		if (*f_path == '/') {
			f_path += 1;
			break;
		}
		f_path--;
	}

	while (*f_path == '/') {
		f_path++;
	}

	debug_print(WARNING, "creating directory %s within %s (hope these strings are good)", f_path, parent_path);

	parent = kopen(parent_path, 0);
	free(parent_path);

	if (!parent) {
		free(path);
		return -ENOENT;
	}

	if (!f_path || !strlen(f_path)) {
		/* Odd edge case with / */
		return -EEXIST;
	}

	fs_node_t * this = kopen(path, 0);
	int _exists = 0;
	if (this) { /* We need to do this because permission check stuff... */
		close_fs(this);
		_exists = 1;
	}

	if (!has_permission(parent, 02)) {
		free(path);
		close_fs(parent);
		return _exists ? -EEXIST : -EACCES;
	}

	int ret = 0;
	if (parent->mkdir) {
		ret = parent->mkdir(parent, f_path, permission);
	} else {
		ret = -EROFS;
	}

	free(path);
	close_fs(parent);

	return ret;
}

fs_node_t *clone_fs(fs_node_t *source) {
	if (!source) return NULL;

	if (source->refcount >= 0) {
		spin_lock(tmp_refcount_lock);
		source->refcount++;
		spin_unlock(tmp_refcount_lock);
	}

	return source;
}

int symlink_fs(char * target, char * name) {
	fs_node_t * parent;
	char *cwd = (char *)(this_core->current_process->wd_name);
	char *path = canonicalize_path(cwd, name);

	char * parent_path = malloc(strlen(path) + 5);
	snprintf(parent_path, strlen(path) + 4, "%s/..", path);

	char * f_path = path + strlen(path) - 1;
	while (f_path > path) {
		if (*f_path == '/') {
			f_path += 1;
			break;
		}
		f_path--;
	}

	debug_print(NOTICE, "creating symlink %s within %s", f_path, parent_path);

	parent = kopen(parent_path, 0);
	free(parent_path);

	if (!parent) {
		free(path);
		return -ENOENT;
	}

	int ret = 0;
	if (parent->symlink) {
		ret = parent->symlink(parent, target, f_path);
	} else {
		ret = -EINVAL;
	}

	free(path);
	close_fs(parent);

	return ret;
}

ssize_t readlink_fs(fs_node_t *node, char * buf, size_t size) {
	if (!node) return -ENOENT;

	if (node->readlink) {
		return node->readlink(node, buf, size);
	} else {
		return -EINVAL;
	}
}


/**
 * @brief Canonicalize a path.
 *
 * @param cwd   Current working directory
 * @param input Path to append or canonicalize on
 * @returns An absolute path string
 */
char *canonicalize_path(const char *cwd, const char *input) {
	/* This is a stack-based canonicalizer; we use a list as a stack */
	list_t *out = list_create("vfs canonicalize_path working memory",input);

	/*
	 * If we have a relative path, we need to canonicalize
	 * the working directory and insert it into the stack.
	 */
	if (strlen(input) && input[0] != PATH_SEPARATOR) {
		/* Make a copy of the working directory */
		char *path = malloc((strlen(cwd) + 1) * sizeof(char));
		memcpy(path, cwd, strlen(cwd) + 1);

		/* Setup tokenizer */
		char *pch;
		char *save;
		pch = strtok_r(path,PATH_SEPARATOR_STRING,&save);

		/* Start tokenizing */
		while (pch != NULL) {
			/* Make copies of the path elements */
			char *s = malloc(sizeof(char) * (strlen(pch) + 1));
			memcpy(s, pch, strlen(pch) + 1);
			/* And push them */
			list_insert(out, s);
			pch = strtok_r(NULL,PATH_SEPARATOR_STRING,&save);
		}
		free(path);
	}

	/* Similarly, we need to push the elements from the new path */
	char *path = malloc((strlen(input) + 1) * sizeof(char));
	memcpy(path, input, strlen(input) + 1);

	/* Initialize the tokenizer... */
	char *pch;
	char *save;
	pch = strtok_r(path,PATH_SEPARATOR_STRING,&save);

	/*
	 * Tokenize the path, this time, taking care to properly
	 * handle .. and . to represent up (stack pop) and current
	 * (do nothing)
	 */
	while (pch != NULL) {
		if (!strcmp(pch,PATH_UP)) {
			/*
			 * Path = ..
			 * Pop the stack to move up a directory
			 */
			node_t * n = list_pop(out);
			if (n) {
				free(n->value);
				free(n);
			}
		} else if (!strcmp(pch,PATH_DOT)) {
			/*
			 * Path = .
			 * Do nothing
			 */
		} else {
			/*
			 * Regular path, push it
			 * XXX: Path elements should be checked for existence!
			 */
			char * s = malloc(sizeof(char) * (strlen(pch) + 1));
			memcpy(s, pch, strlen(pch) + 1);
			list_insert(out, s);
		}
		pch = strtok_r(NULL, PATH_SEPARATOR_STRING, &save);
	}
	free(path);

	/* Calculate the size of the path string */
	size_t size = 0;
	foreach(item, out) {
		/* Helpful use of our foreach macro. */
		size += strlen(item->value) + 1;
	}

	/* join() the list */
	char *output = malloc(sizeof(char) * (size + 1));
	char *output_offset = output;
	if (size == 0) {
		/*
		 * If the path is empty, we take this to mean the root
		 * thus we synthesize a path of "/" to return.
		 */
		output = realloc(output, sizeof(char) * 2);
		output[0] = PATH_SEPARATOR;
		output[1] = '\0';
	} else {
		/* Otherwise, append each element together */
		foreach(item, out) {
			output_offset[0] = PATH_SEPARATOR;
			output_offset++;
			memcpy(output_offset, item->value, strlen(item->value) + 1);
			output_offset += strlen(item->value);
		}
	}

	/* Clean up the various things we used to get here */
	list_destroy(out);
	list_free(out);
	free(out);

	/* And return a working, absolute path */
	return output;
}

void vfs_install(void) {
	/* Initialize the mountpoint tree */
	fs_tree = tree_create();

	struct vfs_entry * root = malloc(sizeof(struct vfs_entry));

	root->name = strdup("[root]");
	root->file = NULL; /* Nothing mounted as root */
	root->fs_type = NULL;
	root->device = NULL;

	tree_set_root(fs_tree, root);

	fs_types = hashmap_create(5);
}

int vfs_register(const char * name, vfs_mount_callback callback) {
	if (hashmap_get(fs_types, name)) return 1;
	hashmap_set(fs_types, name, (void *)(uintptr_t)callback);
	return 0;
}

int vfs_mount_type(const char * type, const char * arg, const char * mountpoint) {

	vfs_mount_callback t = (vfs_mount_callback)(uintptr_t)hashmap_get(fs_types, type);
	if (!t) {
		debug_print(WARNING, "Unknown filesystem type: %s", type);
		return -ENODEV;
	}

	fs_node_t * n = t(arg, mountpoint);

	/* Quick hack to let partition mappers not return a node to mount at 'mountpoint'... */
	if ((uintptr_t)n == (uintptr_t)1) return 0;

	if (!n) return -EINVAL;

	tree_node_t * node = vfs_mount(mountpoint, n);
	if (node && node->value) {
		struct vfs_entry * ent = (struct vfs_entry *)node->value;
		ent->fs_type = strdup(type);
		ent->device  = strdup(arg);
	}

	debug_print(NOTICE, "Mounted %s[%s] to %s: %p", type, arg, mountpoint, (void*)n);
	debug_print_vfs_tree();

	return 0;
}

static spin_lock_t tmp_vfs_lock = { 0 };
/**
 * @brief Mount a file system to the specified path.
 *
 * Mounts a file system node to a given base path.
 * For example, if we have an EXT2 filesystem with a root node
 * of ext2_root and we want to mount it to /, we would run
 * vfs_mount("/", ext2_root); - or, if we have a procfs node,
 * we could mount that to /dev/procfs. Individual files can also
 * be mounted.
 *
 * Paths here must be absolute.
 */
void * vfs_mount(const char * path, fs_node_t * local_root) {
	if (!fs_tree) {
		debug_print(ERROR, "VFS hasn't been initialized, you can't mount things yet!");
		return NULL;
	}
	if (!path || path[0] != '/') {
		debug_print(ERROR, "Path must be absolute for mountpoint.");
		return NULL;
	}

	spin_lock(tmp_vfs_lock);

	local_root->refcount = -1;

	tree_node_t * ret_val = NULL;

	char * p = strdup(path);
	char * i = p;

	int path_len   = strlen(p);

	/* Chop the path up */
	while (i < p + path_len) {
		if (*i == PATH_SEPARATOR) {
			*i = '\0';
		}
		i++;
	}
	/* Clean up */
	p[path_len] = '\0';
	i = p + 1;

	/* Root */
	tree_node_t * root_node = fs_tree->root;

	if (*i == '\0') {
		/* Special case, we're trying to set the root node */
		struct vfs_entry * root = (struct vfs_entry *)root_node->value;
		if (root->file) {
			debug_print(WARNING, "Path %s already mounted, unmount before trying to mount something else.", path);
		}
		root->file = local_root;
		/* We also keep a legacy shortcut around for that */
		fs_root = local_root;
		ret_val = root_node;
	} else {
		tree_node_t * node = root_node;
		char * at = i;
		while (1) {
			if (at >= p + path_len) {
				break;
			}
			int found = 0;
			debug_print(NOTICE, "Searching for %s", at);
			foreach(child, node->children) {
				tree_node_t * tchild = (tree_node_t *)child->value;
				struct vfs_entry * ent = (struct vfs_entry *)tchild->value;
				if (!strcmp(ent->name, at)) {
					found = 1;
					node = tchild;
					ret_val = node;
					break;
				}
			}
			if (!found) {
				debug_print(NOTICE, "Did not find %s, making it.", at);
				struct vfs_entry * ent = malloc(sizeof(struct vfs_entry));
				ent->name = strdup(at);
				ent->file = NULL;
				ent->device = NULL;
				ent->fs_type = NULL;
				node = tree_node_insert_child(fs_tree, node, ent);
			}
			at = at + strlen(at) + 1;
		}
		struct vfs_entry * ent = (struct vfs_entry *)node->value;
		if (ent->file) {
			debug_print(WARNING, "Path %s already mounted, unmount before trying to mount something else.", path);
		}
		ent->file = local_root;
		ret_val = node;
	}

	free(p);
	spin_unlock(tmp_vfs_lock);
	return ret_val;
}

void map_vfs_directory(const char * c) {
	fs_node_t * f = vfs_mapper();
	struct vfs_entry * e = vfs_mount((char*)c, f);
	if (!strcmp(c, "/")) {
		f->device = fs_tree->root;
	} else {
		f->device = e;
	}
}


void debug_print_vfs_tree_node(tree_node_t * node, size_t height) {
	/* End recursion on a blank entry */
	if (!node) return;
	char * tmp = malloc(512);
	memset(tmp, 0, 512);
	char * c = tmp;
	/* Indent output */
	for (uint32_t i = 0; i < height; ++i) {
		c += snprintf(c, 3, "  ");
	}
	/* Get the current process */
	struct vfs_entry * fnode = (struct vfs_entry *)node->value;
	/* Print the process name */
	if (fnode->file) {
		c += snprintf(c, 100, "%s → %s %p (%s, %s)", fnode->name, fnode->device, (void*)fnode->file, fnode->fs_type, fnode->file->name);
	} else {
		c += snprintf(c, 100, "%s → (empty)", fnode->name);
	}
	/* Linefeed */
	debug_print(NOTICE, "%s", tmp);
	free(tmp);
	foreach(child, node->children) {
		/* Recursively print the children */
		debug_print_vfs_tree_node(child->value, height + 1);
	}
}

void debug_print_vfs_tree(void) {
	debug_print_vfs_tree_node(fs_tree->root, 0);
}

/**
 * get_mount_point
 *
 */
fs_node_t *get_mount_point(char * path, unsigned int path_depth, char **outpath, unsigned int * outdepth) {
	size_t depth;

	for (depth = 0; depth <= path_depth; ++depth) {
		path += strlen(path) + 1;
	}

	/* Last available node */
	fs_node_t   * last = fs_root;
	tree_node_t * node = fs_tree->root;

	char * at = *outpath;
	int _depth = 1;
	int _tree_depth = 0;

	while (1) {
		if (at >= path) {
			break;
		}
		int found = 0;
		debug_print(INFO, "Searching for %s", at);
		foreach(child, node->children) {
			tree_node_t * tchild = (tree_node_t *)child->value;
			struct vfs_entry * ent = (struct vfs_entry *)tchild->value;
			if (!strcmp(ent->name, at)) {
				found = 1;
				node = tchild;
				at = at + strlen(at) + 1;
				if (ent->file) {
					_tree_depth = _depth;
					last = ent->file;
					*outpath = at;
				}
				break;
			}
		}
		if (!found) {
			break;
		}
		_depth++;
	}

	*outdepth = _tree_depth;

	if (last) {
		fs_node_t * last_clone = malloc(sizeof(fs_node_t));
		memcpy(last_clone, last, sizeof(fs_node_t));
		last_clone->refcount = 0;
		return last_clone;
	}
	return last;
}



fs_node_t *kopen_recur(const char *filename, uint64_t flags, uint64_t symlink_depth, char *relative_to) {
	/* Simple sanity checks that we actually have a file system */
	if (!filename) {
		return NULL;
	}

	/* Canonicalize the (potentially relative) path... */
	char *path = canonicalize_path(relative_to, filename);
	/* And store the length once to save recalculations */
	size_t path_len = strlen(path);

	/* If strlen(path) == 1, then path = "/"; return root */
	if (path_len == 1) {
		/* Clone the root file system node */
		fs_node_t *root_clone = malloc(sizeof(fs_node_t));
		memcpy(root_clone, fs_root, sizeof(fs_node_t));
		root_clone->refcount = 0;

		/* Free the path */
		free(path);

		open_fs(root_clone, flags);

		/* And return the clone */
		return root_clone;
	}

	/* Otherwise, we need to break the path up and start searching */
	char *path_offset = path;
	uint64_t path_depth = 0;
	while (path_offset < path + path_len) {
		/* Find each PATH_SEPARATOR */
		if (*path_offset == PATH_SEPARATOR) {
			*path_offset = '\0';
			path_depth++;
		}
		path_offset++;
	}
	/* Clean up */
	path[path_len] = '\0';
	path_offset = path + 1;

	/*
	 * At this point, the path is tokenized and path_offset points
	 * to the first token (directory) and path_depth is the number
	 * of directories in the path
	 */

	/*
	 * Dig through the (real) tree to find the file
	 */
	unsigned int depth = 0;
	/* Find the mountpoint for this file */
	fs_node_t *node_ptr = get_mount_point(path, path_depth, &path_offset, &depth);
	debug_print(INFO, "path_offset: %s", path_offset);
	debug_print(INFO, "depth: %d", depth);

	if (!node_ptr) return NULL;

	do {
		/* 
		 * This test is a little complicated, but we basically always resolve symlinks in the
		 * of a path (like /home/symlink/file) even if O_NOFOLLOW and O_PATH are set. If we are
		 * on the leaf of the path then we will look at those flags and act accordingly
		 */
		if ((node_ptr->flags & FS_SYMLINK) &&
				!((flags & O_NOFOLLOW) && (flags & O_PATH) && depth == path_depth)) {
			/* This ensures we don't return a path when NOFOLLOW is requested but PATH
			 * isn't passed.
			 */
			debug_print(NOTICE, "resolving symlink at %s", node_ptr->name);
			if ((flags & O_NOFOLLOW) && depth == path_depth - 1) {
				/* TODO(gerow): should probably be setting errno from this */
				debug_print(NOTICE, "Refusing to follow final entry for open with O_NOFOLLOW for %s.", node_ptr->name);
				free((void *)path);
				free(node_ptr);
				return NULL;
			}
			if (symlink_depth >= MAX_SYMLINK_DEPTH) {
				/* TODO(gerow): should probably be setting errno from this */
				debug_print(WARNING, "Reached max symlink depth on %s.", node_ptr->name);
				free((void *)path);
				free(node_ptr);
				return NULL;
			}
			/* 
			 * This may actually be big enough that we wouldn't want to allocate it on
			 * the stack, especially considering this function is called recursively
			 */
			char symlink_buf[MAX_SYMLINK_SIZE];
			int len = readlink_fs(node_ptr, symlink_buf, sizeof(symlink_buf));
			if (len < 0) {
				/* TODO(gerow): should probably be setting errno from this */
				debug_print(WARNING, "Got error %d from symlink for %s.", len, node_ptr->name);
				free((void *)path);
				free(node_ptr);
				return NULL;
			}
			if (symlink_buf[len] != '\0') {
				/* TODO(gerow): should probably be setting errno from this */
				debug_print(WARNING, "readlink for %s doesn't end in a null pointer. That's weird...", node_ptr->name);
				free((void *)path);
				free(node_ptr);
				return NULL;
			}
			fs_node_t * old_node_ptr = node_ptr;
			/* Rebuild our path up to this point. This is hella hacky. */
			char * relpath = malloc(path_len + 1);
			char * ptr = relpath;
			memcpy(relpath, path, path_len + 1);
			for (unsigned int i = 0; depth && i < depth-1; i++) {
				while(*ptr != '\0') {
					ptr++;
				}
				*ptr = PATH_SEPARATOR;
			}
			node_ptr = kopen_recur(symlink_buf, 0, symlink_depth + 1, relpath);
			free(relpath);
			free(old_node_ptr);
			if (!node_ptr) {
				/* Dangling symlink? */
				debug_print(WARNING, "Failed to open symlink path %s. Perhaps it's a dangling symlink?", symlink_buf);
				free((void *)path);
				return NULL;
			}
		}
		if (path_offset >= path+path_len) {
			free(path);
			open_fs(node_ptr, flags);
			return node_ptr;
		}
		if (depth == path_depth) {
			/* We found the file and are done, open the node */
			open_fs(node_ptr, flags);
			free((void *)path);
			return node_ptr;
		}
		/* We are still searching... */
		debug_print(INFO, "... Searching for %s", path_offset);
		fs_node_t * node_next = finddir_fs(node_ptr, path_offset);
		free(node_ptr); /* Always a clone or an unopened thing */
		node_ptr = node_next;
		/* Search the active directory for the requested directory */
		if (!node_ptr) {
			/* We failed to find the requested directory */
			free((void *)path);
			return NULL;
		}
		path_offset += strlen(path_offset) + 1;
		++depth;
	} while (depth < path_depth + 1);
	debug_print(INFO, "- Not found.");
	/* We failed to find the requested file, but our loop terminated. */
	free((void *)path);
	return NULL;
}

/**
 * @brief Open a file by name.
 *
 * Explore the file system tree to find the appropriate node for
 * for a given path. The path can be relative to the working directory
 * and will be canonicalized by the kernel.
 *
 * @param filename Filename to open
 * @param flags    Flag bits for read/write mode.
 * @returns A file system node element that the caller can free.
 */
fs_node_t *kopen(const char *filename, unsigned int flags) {
	debug_print(NOTICE, "kopen(%s)", filename);

	return kopen_recur(filename, flags, 0, (char *)(this_core->current_process->wd_name));
}

