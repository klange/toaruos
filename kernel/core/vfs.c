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

uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	if (node->read != 0) {
		uint32_t ret = node->read(node, offset, size, buffer);
		return ret;
	} else {
		return 0;
	}
}

uint32_t write_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	if (node->write != 0) {
		uint32_t ret = node->write(node, offset, size, buffer);
		return ret;
	} else {
		return 0;
	}
}

void open_fs(fs_node_t *node, uint8_t read, uint8_t write) {
	if (node->open != 0) {
		node->open(node, read, write);
	}
}

void close_fs(fs_node_t *node) {
	if (node == fs_root) { 
		HALT_AND_CATCH_FIRE("Attemped to close the filesystem root. kablooey", NULL);
	}
	if (node->close != 0) {
		node->close(node);
	}
}

struct dirent * readdir_fs(fs_node_t *node, uint32_t index) {
	if ((node->flags & FS_DIRECTORY) && node->readdir != NULL) {
		struct dirent * ret = node->readdir(node, index);
		return ret;
	} else {
		return (struct dirent *)NULL;
	}
}

fs_node_t *finddir_fs(fs_node_t *node, char *name) {
	if ((node->flags & FS_DIRECTORY) && node->finddir != NULL) {
		fs_node_t * ret = node->finddir(node, name);
		return ret;
	} else {
		return (fs_node_t *)NULL;
	}
}

fs_node_t * clone_fs(fs_node_t * source) {
	if (!source) {
		return NULL;
	}
	fs_node_t * n = malloc(sizeof(fs_node_t));
	memcpy(n, source, sizeof(fs_node_t));
	return n;
}

/*
 * Canonicalize a path.
 */
char *
canonicalize_path(char *cwd, char *input) {
	list_t * out = list_create();

	if (strlen(input) && input[0] != '/') {
		char * path = malloc((strlen(cwd) + 1) * sizeof(char));
		memcpy(path, cwd, strlen(cwd) + 1);

		char * pch;
		char * save;
		pch = strtok_r(path,"/",&save);

		while (pch != NULL) {
			char * s = malloc(sizeof(char) * (strlen(pch) + 1));
			memcpy(s, pch, strlen(pch) + 1);
			list_insert(out, s);
			pch = strtok_r(NULL,"/",&save);
		}
		free(path);
	}

	char * path = malloc((strlen(input) + 1) * sizeof(char));
	memcpy(path, input, strlen(input) + 1);
	char * pch;
	char * save;
	pch = strtok_r(path,"/",&save);
	while (pch != NULL) {
		if (!strcmp(pch,"..")) {
			node_t * n = list_pop(out);
			if (n) {
				free(n->value);
				free(n);
			}
		} else if (!strcmp(pch,".")) {
			/* pass */
		} else {
			char * s = malloc(sizeof(char) * (strlen(pch) + 1));
			memcpy(s, pch, strlen(pch) + 1);
			list_insert(out, s);
		}
		pch = strtok_r(NULL, "/", &save);
	}
	free(path);

	size_t size = 0;

	foreach(item, out) {
		size += strlen(item->value) + 1;
	}

	char * output = malloc(sizeof(char) * (size + 1));
	char * output_offset = output;
	if (size == 0) {
		output = realloc(output, sizeof(char) * 2);
		output[0] = '/';
		output[1] = '\0';
	} else {
		foreach(item, out) {
			output_offset[0] = '/';
			output_offset++;
			memcpy(output_offset, item->value, strlen(item->value) + 1);
			output_offset += strlen(item->value);
		}
	}

	list_destroy(out);
	list_free(out);
	free(out);

	return output;
}

/*
 * Retreive the node for the requested path
 * HACK FIXME XXX TODO
 * THIS IS A TERRIBLE HACK OF A FUNCTION AND IT SHOULD
 * BE FIX OR ELSE EVERYTHING ELSE WILL BE HORRIBLY BROKEN!
 */
fs_node_t *
kopen(
		char *filename,
		uint32_t flags
	 ) {
	/* Some sanity checks */
	if (!fs_root || !filename) {
		return NULL;
	}
	char * cwd = (char *)(current_process->wd_name);
	char * npath = canonicalize_path(cwd, filename);
	size_t path_len = strlen(npath);
	if (path_len == 1) {
		fs_node_t * root_clone = malloc(sizeof(fs_node_t));
		memcpy(root_clone, fs_root, sizeof(fs_node_t));
		return root_clone;
	}
	char * path = (char *)malloc(sizeof(char) * (path_len + 1));
	memcpy(path, npath, path_len + 1);
	free(npath);
	char * path_offset = path;
	uint32_t path_depth = 0;
	while (path_offset < path + path_len) {
		if (*path_offset == '/') {
			*path_offset = '\0';
			path_depth++;
		}
		path_offset++;
	}
	path[path_len] = '\0';
	path_offset = path + 1;
	uint32_t depth;
	fs_node_t * node_ptr = malloc(sizeof(fs_node_t));
	memcpy(node_ptr, fs_root, sizeof(fs_node_t));
	fs_node_t * node_next = NULL;
	for (depth = 0; depth < path_depth; ++depth) {
		node_next = finddir_fs(node_ptr, path_offset);
		free(node_ptr);
		node_ptr = node_next;
		if (!node_ptr) {
			free((void *)path);
			return NULL;
		} else if (depth == path_depth - 1) {
			open_fs(node_ptr, 1, 0);
			free((void *)path);
			return node_ptr;
		}
		path_offset += strlen(path_offset) + 1;
	}
	free((void *)path);
	return NULL;
}


