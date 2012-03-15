/* vim: shiftwidth=4 tabstop=4 noexpandtab
 *
 * Virtual File System
 *
 */
#include <system.h>
#include <fs.h>
#include <list.h>
#include <process.h>

fs_node_t *fs_root = 0;

/**
 * read_fs: Read a file system node based on its underlying type.
 *
 * @param node    Node to read
 * @param offset  Offset into the node data to read from
 * @param size    How much data to read (in bytes)
 * @param buffer  A buffer to copy of the read data into
 * @returns Bytes read
 */
uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	if (node->read) {
		uint32_t ret = node->read(node, offset, size, buffer);
		return ret;
	} else {
		return 0;
	}
}

/**
 * write_fs: Write a file system node based on its underlying type.
 *
 * @param node    Node to write to
 * @param offset  Offset into the node data to write to
 * @param size    How much data to write (in bytes)
 * @param buffer  A buffer to copy from
 * @returns Bytes written
 */
uint32_t write_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	if (node->write) {
		uint32_t ret = node->write(node, offset, size, buffer);
		return ret;
	} else {
		return 0;
	}
}

/**
 * open_fs: Open a file system node.
 *
 * @param node  Node to open
 * @param read  Read permission? (1 = yes)
 * @param write Write permission? (1 = overwrite, 2 = append)
 */
void open_fs(fs_node_t *node, uint8_t read, uint8_t write) {
	if (node->open) {
		node->open(node, read, write);
	}
}

/**
 * close_fs: Close a file system node
 *
 * @param node Node to close
 */
void close_fs(fs_node_t *node) {
	assert(node != fs_root && "Attempted to close the filesystem root. kablooey");
	if (node->close) {
		node->close(node);
	}
}

/**
 * readdir_fs: Read a directory for the requested index
 *
 * @param node  Directory to read
 * @param index Offset to look for
 * @returns A dirent object.
 */
struct dirent *readdir_fs(fs_node_t *node, uint32_t index) {
	if ((node->flags & FS_DIRECTORY) && node->readdir) {
		struct dirent *ret = node->readdir(node, index);
		return ret;
	} else {
		return (struct dirent *)NULL;
	}
}

/**
 * finddir_fs: Find the requested file in the directory and return an fs_node for it
 *
 * @param node Directory to search
 * @param name File to look for
 * @returns An fs_node that the caller can free
 */
fs_node_t *finddir_fs(fs_node_t *node, char *name) {
	if ((node->flags & FS_DIRECTORY) && node->finddir) {
		fs_node_t *ret = node->finddir(node, name);
		return ret;
	} else {
		return (fs_node_t *)NULL;
	}
}

/*
 * XXX: The following two function should be replaced with
 *      one function to create children of directory nodes.
 *      There is no fundamental difference between a directory
 *      and a file, thus, the use of flag sets should suffice
 */

int create_file_fs(char *name, uint16_t permission) {
	int32_t i = strlen(name);
	char *dir_name = malloc(i + 1);
	memcpy(dir_name, name, i);
	dir_name[i] = '\0';
	if (dir_name[i - 1] == '/')
		dir_name[i - 1] = '\0';
	if (strlen(dir_name) == 0) {
		free(dir_name);
		return 1;
	}
	for (i = strlen(dir_name) - 1; i >= 0; i--) {
		if (dir_name[i] == '/') {
			dir_name[i] = '\0';
			break;
		}
	}

	// get the parent dir node.
	fs_node_t *node;
	if (i >= 0) {
		node = kopen(dir_name, 0);
	} else {
		/* XXX This is wrong */
		node = kopen(".", 0);
	}

	if (node == NULL) {
		free(dir_name);
		return 2;
	}

	i++;
	if ((node->flags & FS_DIRECTORY) && node->mkdir) {
		node->create(node, dir_name + i, permission);
	}

	free(node);
	free(dir_name);
	return 0;
}

int mkdir_fs(char *name, uint16_t permission) {
	int32_t i = strlen(name);
	char *dir_name = malloc(i + 1);
	memcpy(dir_name, name, i);
	dir_name[i] = '\0';
	if (dir_name[i - 1] == '/')
		dir_name[i - 1] = '\0';
	if (strlen(dir_name) == 0) {
		free(dir_name);
		return 1;
	}
	for (i = strlen(dir_name) - 1; i >= 0; i--) {
		if (dir_name[i] == '/') {
			dir_name[i] = '\0';
			break;
		}
	}

	// get the parent dir node.
	fs_node_t *node;
	if (i >= 0) {
		node = kopen(dir_name, 0);
	} else {
		node = kopen(".", 0);
	}

	if (node == NULL) {
		kprintf("mkdir: Directory does not exist\n");
		free(dir_name);
		return 1;
	}

	i++;
	if ((node->flags & FS_DIRECTORY) && node->mkdir) {
		node->mkdir(node, dir_name + i, permission);
	}

	free(node);
	free(dir_name);

	return 0;
}

fs_node_t *clone_fs(fs_node_t *source) {
	if (!source) {
		return NULL;
	}
	fs_node_t *n = malloc(sizeof(fs_node_t));
	memcpy(n, source, sizeof(fs_node_t));
	return n;
}

/**
 * canonicalize_path: Canonicalize a path.
 *
 * @param cwd   Current working directory
 * @param input Path to append or canonicalize on
 * @returns An absolute path string
 */
char *canonicalize_path(char *cwd, char *input) {
	/* This is a stack-based canonicalizer; we use a list as a stack */
	list_t *out = list_create();

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

/**
 * get_mount_point
 *
 */
fs_node_t *get_mount_point(char * path, size_t path_depth) {
	size_t depth;

	kprintf("[root]");
	for (depth = 0; depth <= path_depth; ++depth) {
		kprintf("%s%c", path, (depth == path_depth) ? '\n' : '/');
		path += strlen(path) + 1;
	}

#if 0
	tree_node_t * tnode = from;
	foreach(node, tnode->children) {
		tree_node_t * _node = (tree_node_t *)node->value;
		shm_node_t *  snode = (shm_node_t *)_node->value;

		if (!strcmp(snode->name, pch)) {
			if (*save == '\0') {
				return snode;
			}
			return _get_node(save, create, _node);
		}
	}
#endif

#if 0
	for (depth = 0; depth < path_depth; ++depth) {
		/* Search the active directory for the requested directory */
		node_next = finddir_fs(node_ptr, path_offset);
		free(node_ptr);
		node_ptr = node_next;
		if (!node_ptr) {
			/* We failed to find the requested directory */
			/* XXX: This is where we should be checking other file system mappings */
			free((void *)path);
			return NULL;
		} else if (depth == path_depth - 1) {
			/* We found the file and are done, open the node */
			open_fs(node_ptr, 1, 0);
			free((void *)path);
			return node_ptr;
		}
		/* We are still searching... */
		path_offset += strlen(path_offset) + 1;
	}
#endif

	return fs_root;
}



/**
 * kopen: Open a file by name.
 *
 * Explore the file system tree to find the appropriate node for
 * for a given path. The path can be relative to the working directory
 * and will be canonicalized by the kernel.
 *
 * @param filename Filename to open
 * @param flags    Flag bits for read/write mode.
 * @returns A file system node element that the caller can free.
 */
fs_node_t *kopen(char *filename, uint32_t flags) {
	/* Simple sanity checks that we actually have a file system */
	if (!fs_root || !filename) {
		return NULL;
	}

	/* Reference the current working directory */
	char *cwd = (char *)(current_process->wd_name);
	/* Canonicalize the (potentially relative) path... */
	char *path = canonicalize_path(cwd, filename);
	/* And store the length once to save recalculations */
	size_t path_len = strlen(path);

	/* If strlen(path) == 1, then path = "/"; return root */
	if (path_len == 1) {
		/* Clone the root file system node */
		fs_node_t *root_clone = malloc(sizeof(fs_node_t));
		memcpy(root_clone, fs_root, sizeof(fs_node_t));

		/* Free the path */
		free(path);

		/* And return the clone */
		return root_clone;
	}

	/* Otherwise, we need to break the path up and start searching */
	char *path_offset = path;
	uint32_t path_depth = 0;
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
	uint32_t depth;
	fs_node_t *node_ptr = malloc(sizeof(fs_node_t));
	/* Find the mountpoint for this file */
	fs_node_t *mount_point = get_mount_point(path, path_depth);
	/* Set the active directory to the mountpoint */
	memcpy(node_ptr, mount_point, sizeof(fs_node_t));
	fs_node_t *node_next = NULL;
	for (depth = 0; depth < path_depth; ++depth) {
		/* Search the active directory for the requested directory */
		node_next = finddir_fs(node_ptr, path_offset);
		free(node_ptr);
		node_ptr = node_next;
		if (!node_ptr) {
			/* We failed to find the requested directory */
			free((void *)path);
			return NULL;
		} else if (depth == path_depth - 1) {
			/* We found the file and are done, open the node */
			open_fs(node_ptr, 1, 0);
			free((void *)path);
			return node_ptr;
		}
		/* We are still searching... */
		path_offset += strlen(path_offset) + 1;
	}
	/* We failed to find the requested file, but our loop terminated. */
	free((void *)path);
	return NULL;
}

