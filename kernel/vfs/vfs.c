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

#define debug_print(x, fmt, ...) do { if (0) {dprintf("vfs.c [" #x "] " fmt "\n", ## __VA_ARGS__); } } while (0)

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

	uid_t whom  = this_core->current_process->user;
	gid_t whomg = this_core->current_process->group;
	if (permission_bit & 010) {
		whom = this_core->current_process->real_user;
		whomg = this_core->current_process->real_user_group;
		permission_bit &= ~010;
	}

	if (whom == USER_ROOT_UID) {
		if (!(permission_bit & 01)) return 1;
		if (node->flags & FS_DIRECTORY) return 1;
	}

	uint64_t permissions = node->mask;

	uint8_t my_permissions = (permissions) & 07;
	uint8_t user_perm  = (permissions >> 6) & 07;
	uint8_t group_perm = (permissions >> 3) & 07;

	if (whom  == node->uid) my_permissions |= user_perm;
	if (whomg == node->gid) my_permissions |= group_perm;
	else if (this_core->current_process->supplementary_group_count) {
		for (int i = 0; i < this_core->current_process->supplementary_group_count; ++i) {
			if (this_core->current_process->supplementary_group_list[i] == node->gid) {
				my_permissions |= group_perm;
			}
		}
	}

	return (permission_bit & my_permissions) == permission_bit;
}

static int readdir_mapper(fs_node_t *node, unsigned long index, struct dirent * dir) {
	tree_node_t * d = (tree_node_t *)node->device;

	if (!d) return 0;

	if (index == 0) {
		strcpy(dir->d_name, ".");
		dir->d_ino = 0;
		return 1;
	} else if (index == 1) {
		strcpy(dir->d_name, "..");
		dir->d_ino = 1;
		return 1;
	}

	index -= 2;
	unsigned long i = 0;
	foreach(child, d->children) {
		if (i == index) {
			/* Recursively print the children */
			tree_node_t * tchild = (tree_node_t *)child->value;
			struct vfs_entry * n = (struct vfs_entry *)tchild->value;

			size_t len = strlen(n->name) + 1;
			memcpy(&dir->d_name, n->name, MIN(256, len));
			dir->d_ino = i;
			return 1;
		}
		++i;
	}

	return 0;
}

static fs_node_t * vfs_mapper(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->mask    = 0555;
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
		if (node->flags & FS_DIRECTORY) return -EISDIR;
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
		if (node->flags & FS_DIRECTORY) return -EISDIR;
		return -EROFS;
	}
}

/**
 * @brief set the size of a file to 9
 *
 * @param node File to resize
 */
int truncate_fs(fs_node_t * node, size_t size) {
	if (!node) return -ENOENT;

	if (node->truncate) {
		return node->truncate(node, size);
	}

	return -EINVAL;
}

//volatile uint8_t tmp_refcount_lock = 0;
static spin_lock_t tmp_refcount_lock = { 0 };

void vfs_lock(fs_node_t * node) {
	spin_lock(tmp_refcount_lock);
	/* We no longer immortalize things in the mount tree, just increment the refcount */
	node->refcount += 1;
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

	spin_lock(tmp_refcount_lock);
	if (node->refcount >= 0) {
		node->refcount++;
	}
	spin_unlock(tmp_refcount_lock);

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

	spin_lock(tmp_refcount_lock);
	if (node->refcount < 0) {
		spin_unlock(tmp_refcount_lock);
		return;
	}

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
int readdir_fs(fs_node_t *node, unsigned long index, struct dirent * out) {
	if (!node) return -EINVAL;

	if ((node->flags & FS_DIRECTORY) && node->readdir) {
		return node->readdir(node, index, out);
	} else {
		return -EINVAL;
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
	return node->ioctl ? node->ioctl(node, request, argp) : -ENOTTY;
}

fs_node_t * file_get_parent(const char * path, int *error) {
	char * parent_path = malloc(strlen(path) + 5);
	snprintf(parent_path, strlen(path) + 4, "%s/..", path);
	fs_node_t * parent  = kopen_error(parent_path, 0, error);
	free(parent_path);
	return parent;
}

static const char * fs_basename(const char * path) {
	const char * f_path = path + strlen(path) - 1;
	while (f_path > path && *f_path == '/') {
		/* Trailing slashes */
		f_path--;
	}
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

	return f_path;
}

int rename_file_fs(const char * src, const char * dest) {
	int error = 0;
	if (!*src || !*dest) return -ENOENT;
	fs_node_t * src_parent = file_get_parent(src, &error);
	if (!src_parent) return -error;
	fs_node_t * dest_parent = file_get_parent(dest, &error);
	if (!dest_parent) { close_fs(src_parent); return -error; }

	int out = 0;
	if (!src_parent->mount) { out = -EROFS; goto _nope; }
	if (src_parent->mount != dest_parent->mount) { out = -EXDEV; goto _nope; }
	if (!src_parent->mount->rename) { out = -ENOTSUP; goto _nope; }

	if (!has_permission(src_parent, 02) || !has_permission(src_parent, 01)) { out = -EACCES; goto _nope; }
	if (!has_permission(dest_parent, 02) || !has_permission(dest_parent, 01)) { out = -EACCES; goto _nope; }

	/* Get basename of each path component */
	const char * src_name = fs_basename(src);
	const char * dest_name = fs_basename(dest);

	if (!*src_name || !*dest_name) return -EINVAL;
	if (*src_name == '/' || *dest_name == '/') return -EINVAL;

	out = src_parent->mount->rename(src_parent->mount, src_parent, src_name, dest_parent, dest_name);

_nope:
	close_fs(dest_parent);
	close_fs(src_parent);
	return out;
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

	int error = 0;
	parent = kopen_error(parent_path, 0, &error);
	free(parent_path);

	if (!parent) {
		debug_print(WARNING, "failed to open parent");
		free(path);
		return -error;
	}

	/* Need both exec and write on the parent to create a new entry */
	if (!has_permission(parent, 02) || !has_permission(parent, 01)) {
		free(path);
		close_fs(parent);
		return -EACCES;
	}

	int ret = 0;
	if (parent->create) {
		ret = parent->create(parent, f_path, permission);
	} else {
		ret = -EINVAL;
	}

	free(path);
	close_fs(parent);

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

	int error = 0;
	parent = kopen_error(parent_path, 0, &error);
	free(parent_path);

	if (!parent) {
		free(path);
		return -error;
	}

	if (!has_permission(parent, 02) || !has_permission(parent, 01)) {
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

	int error = 0;
	parent = kopen_error(parent_path, 0, &error);
	free(parent_path);

	if (!parent) {
		free(path);
		return -error;
	}

	if (!f_path || !strlen(f_path)) {
		/* Odd edge case with / */
		return -EEXIST;
	}

	/* Permission check was moved into methods for reasons. */

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

	spin_lock(tmp_refcount_lock);
	if (source->refcount >= 0) {
		source->refcount++;
	}
	spin_unlock(tmp_refcount_lock);

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

	int error = 0;
	parent = kopen_error(parent_path, 0, &error);
	free(parent_path);

	if (!parent) {
		free(path);
		return -error;
	}

	/* Need both exec and write on the parent to create a new entry */
	if (!has_permission(parent, 02) || !has_permission(parent, 01)) {
		free(path);
		close_fs(parent);
		return -EACCES;
	}

	int ret = 0;
	if (parent->symlink) {
		ret = parent->symlink(parent, target, f_path);
	} else {
		ret = -EPERM;
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
		return -EPERM;
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
	if (*input && *input != PATH_SEPARATOR) {
		/* Make a copy of the working directory */
		char *path = strdup(cwd);

		/* Setup tokenizer */
		char *pch;
		char *save;
		pch = strtok_r(path,PATH_SEPARATOR_STRING,&save);

		/* Start tokenizing */
		while (pch != NULL) {
			list_insert(out, strdup(pch));
			pch = strtok_r(NULL,PATH_SEPARATOR_STRING,&save);
		}
		free(path);
	}

	/* Similarly, we need to push the elements from the new path */
	char *path = strdup(input);

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
			/* Pop */
			node_t * n = list_pop(out);
			if (n) {
				free(n->value);
				free(n);
			}
		} else if (!strcmp(pch,PATH_DOT)) {
			/* Ignore dot */
		} else {
			list_insert(out, strdup(pch));
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

	tree_node_t * node = vfs_mount(mountpoint, n, type, arg);
	if (!node) return -EINVAL;

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
void * vfs_mount(const char * path, fs_node_t * local_root, const char * type, const char * options) {
	if (!fs_tree) {
		debug_print(ERROR, "VFS hasn't been initialized, you can't mount things yet!");
		return NULL;
	}
	if (!path || path[0] != '/') {
		debug_print(ERROR, "Path must be absolute for mountpoint.");
		return NULL;
	}

	spin_lock(tmp_vfs_lock);

	vfs_lock(local_root);

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
		root->device = strdup(options);
		root->fs_type = strdup(type);
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
		ent->device = strdup(options);
		ent->fs_type = strdup(type);
		ret_val = node;
	}

	free(p);
	spin_unlock(tmp_vfs_lock);
	return ret_val;
}

void map_vfs_directory(const char * c) {
	fs_node_t * f = vfs_mapper();
	tree_node_t * e = vfs_mount((char*)c, f, "vfs_mapper", "");
	if (!strcmp(c, "/")) {
		f->device = fs_tree->root;
	} else {
		f->device = e;
	}
}


static void debug_print_vfs_tree_node(tree_node_t * node, size_t height) {
	/* End recursion on a blank entry */
	if (!node) return;
#ifdef MISAKA_DEBUG_PRINT_VFS_TREE
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
#endif
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
		if (at >= path) break;
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
		if (!found) break;
		_depth++;
	}

	*outdepth = _tree_depth;

	return last;
}

static char * path_tokenize(char * path, size_t len, unsigned int *depth) {
	char *path_offset = path;
	*depth = 0;
	while (path_offset < path + len) {
		/* Find each PATH_SEPARATOR */
		if (*path_offset == PATH_SEPARATOR) {
			*path_offset = '\0';
			(*depth)++;
		}
		path_offset++;
	}
	/* Clean up */
	path[len] = '\0';
	return path + 1;
}

static char * path_untokenize(char * path, size_t len, unsigned int depth) {
	char * out = malloc(len + 1);
	memcpy(out, path, len + 1);
	char * p = out;
	for (unsigned int i = 0; depth && i < depth - 1; i++) {
		while (*p) p++;
		*p = PATH_SEPARATOR;
	}
	return out;
}


fs_node_t *kopen_recur(const char *filename, uint64_t flags, uint64_t symlink_depth, char *relative_to, int * error) {
	/* Simple sanity checks that we actually have a file system */
	if (!filename) return *error = ENOENT, NULL;

	/* Canonicalize the (potentially relative) path... */
	char *path = canonicalize_path(relative_to, filename);
	/* And store the length once to save recalculations */
	size_t path_len = strlen(path);

	/* If strlen(path) == 1, then path = "/"; return root */
	if (path_len == 1) {
		free(path);
		open_fs(fs_root, flags);
		return fs_root;
	}

	/* Otherwise, we need to break the path up and start searching */
	unsigned int path_depth = 0;
	char * path_offset = path_tokenize(path, path_len, &path_depth);

	unsigned int depth = 0;
	fs_node_t *node_ptr = get_mount_point(path, path_depth, &path_offset, &depth);

	if (!node_ptr) return *error = ENOENT, NULL;
	open_fs(node_ptr, flags);

	do {
		if ((node_ptr->flags & FS_SYMLINK) && !((flags & O_NOFOLLOW) && depth == path_depth)) {
			if (symlink_depth >= MAX_SYMLINK_DEPTH) return *error = ELOOP, free(path), close_fs(node_ptr), NULL;
			char *symlink_buf = calloc(MAX_SYMLINK_SIZE, 1);

			int len = readlink_fs(node_ptr, symlink_buf, MAX_SYMLINK_SIZE);
			close_fs(node_ptr); /* We don't need the reference to the original symlink anymore.*/

			if (len < 0) return free(symlink_buf), *error = -len, free(path), NULL; /* Could not read symlink */
			if (!len || symlink_buf[len]) return free(symlink_buf), *error = ENOENT, free(path), NULL; /* Symlink name truncated = same as couldn't read */

			/* Rebuild our path up to this point. This is hella hacky. */
			char * relpath = path_untokenize(path, path_len, depth);

			symlink_depth += 1;
			node_ptr = kopen_recur(symlink_buf, 0, symlink_depth, relpath, error);
			free(symlink_buf);
			free(relpath);

			if (!node_ptr) return free((void *)path), NULL;
		}

		/* Found what we were looking for. */
		if (path_offset >= path+path_len || depth == path_depth) return free(path), node_ptr;

		/* We are still searching, so this needs to be a directory. */
		if (!(node_ptr->flags & FS_DIRECTORY)) return *error = ENOTDIR, free(path), close_fs(node_ptr), NULL;
		if (!has_permission(node_ptr, 01)) return *error = EACCES, free(path), close_fs(node_ptr), NULL;

		/* Search for the requested file. */
		fs_node_t * node_next = finddir_fs(node_ptr, path_offset);
		close_fs(node_ptr);

		if (!node_next) return free(path), *error = ENOENT, NULL;
		node_ptr = node_next;
		open_fs(node_ptr, flags);

		path_offset += strlen(path_offset) + 1;
		++depth;
	} while (depth < path_depth + 1);

	free(path);
	*error = ENOENT;
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
 * @param error    Where to place an error number, when returning NULL.
 * @returns A file system node element that the caller can free.
 */
fs_node_t *kopen_error(const char *filename, unsigned int flags, int *error) {
	*error = 0;
	return kopen_recur(filename, flags, 0, (char *)(this_core->current_process->wd_name), error);
}

fs_node_t *kopen(const char *filename, unsigned int flags) {
	int error = 0;
	return kopen_error(filename, flags, &error);
}
