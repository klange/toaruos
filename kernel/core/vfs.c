#include <system.h>
#include <fs.h>

fs_node_t *fs_root = 0;

uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	if (node->read != 0) {
		return node->read(node, offset, size, buffer);
	} else {
		return 0;
	}
}

uint32_t write_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	if (node->write != 0) {
		return node->write(node, offset, size, buffer);
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
	if (node->close != 0) {
		node->close(node);
	}
}

struct dirent * readdir_fs(fs_node_t *node, uint32_t index) {
	if ((node->flags & FS_DIRECTORY) && node->readdir != NULL) {
		return node->readdir(node, index);
	} else {
		return (struct dirent *)NULL;
	}
}

fs_node_t *finddir_fs(fs_node_t *node, char *name) {
	if ((node->flags & FS_DIRECTORY) && node->readdir != NULL) {
		return node->finddir(node, name);
	} else {
		return (fs_node_t *)NULL;
	}
}

/*
 * Retreive the node for the requested path
 */
fs_node_t *
kopen(
		const char *filename,
		uint32_t flags
	 ) {
	/* let's do this shit */
	if (!fs_root) {
		HALT_AND_CATCH_FIRE("Attempted to kopen() without a filesystem in place.", NULL);
	}
	if (!filename) {
		HALT_AND_CATCH_FIRE("Attempted to kopen() without a filename.", NULL);
	}
	if (filename[0] != '/') {
		HALT_AND_CATCH_FIRE("Attempted to kopen() a non-absolute path.", NULL);
	}
	size_t path_len = strlen(filename);
	if (path_len == 1) {
		return fs_root;
	}
	char * path = (char *)malloc(sizeof(char) * (path_len + 1));
	memcpy(path, filename, path_len);
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
	fs_node_t * node_ptr = fs_root;
	for (depth = 0; depth < path_depth; ++depth) {
		node_ptr = finddir_fs(node_ptr, path_offset);
		if (!node_ptr) {
			free((void *)path);
			return NULL;
		} else if (depth == path_depth - 1) {
			open_fs(node_ptr, 1, 0);
			return node_ptr;
		}
		path_offset += strlen(path_offset) + 1;
	}
	free((void *)path);
	return NULL;
}


