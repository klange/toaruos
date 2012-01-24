/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * Buffered Pipe
 */

#include <system.h>
#include <fs.h>
#include <pipe.h>

uint32_t read_pipe(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_pipe(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_pipe(fs_node_t *node, uint8_t read, uint8_t write);
void close_pipe(fs_node_t *node);

static inline size_t pipe_unread(pipe_device_t * pipe) {
	if (pipe->read_ptr > pipe->write_ptr) {
		return (pipe->size - pipe->read_ptr) + pipe->write_ptr;
	} else {
		return (pipe->write_ptr - pipe->read_ptr);
	}
}

static inline size_t pipe_available(pipe_device_t * pipe) {
	if (pipe->read_ptr > pipe->write_ptr) {
		return (pipe->read_ptr - pipe->write_ptr);
	} else {
		return (pipe->size - pipe->write_ptr)+ pipe->read_ptr;
	}
}

static inline void pipe_increment_read(pipe_device_t * pipe) {
	pipe->read_ptr++;
	if (pipe->read_ptr == pipe->size) {
		pipe->read_ptr = 0;
	}
}

static inline void pipe_increment_write(pipe_device_t * pipe) {
	pipe->write_ptr++;
	if (pipe->write_ptr == pipe->size) {
		pipe->write_ptr = 0;
	}
}

uint32_t read_pipe(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	assert(node->inode != 0 && "Attempted to read from a fully-closed pipe.");

	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->inode;

	size_t collected = 0;
	while (collected < size) {
		while (pipe_unread(pipe) > 0) {
			buffer[collected] = pipe->buffer[pipe->read_ptr];
			pipe_increment_read(pipe);
			collected++;
		}
		switch_task();
	}

	return size;
}

uint32_t write_pipe(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	assert(node->inode != 0 && "Attempted to write to a fully-closed pipe.");

	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->inode;

	size_t written = 0;
	while (written < size) {
		while (pipe_available(pipe) > 0) {
			pipe->buffer[pipe->write_ptr] = buffer[written];
			pipe_increment_write(pipe);
			written++;
		}
		switch_task();
	}

	return size;
}

void open_pipe(fs_node_t * node, uint8_t read, uint8_t write) {
	assert(node->inode != 0 && "Attempted to open a fully-closed pipe.");

	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->inode;

	/* Add a reference */
	pipe->refcount++;

	return;
}

void close_pipe(fs_node_t * node) {
	assert(node->inode != 0 && "Attempted to close an already fully-closed pipe.");

	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->inode;

	/* Drop one reference */
	pipe->refcount--;

	/* Check the reference count number */
	if (pipe->refcount == 0) {
		/* No other references exist, free the pipe (but not its buffer) */
		free(pipe);
		/* And let the creator know there are no more references */
		node->inode = NULL;
	}

	return;
}

fs_node_t * make_pipe(uint8_t * buffer, size_t size) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	pipe_device_t * pipe = malloc(sizeof(fs_node_t));

	fnode->inode = 0;
	fnode->name[0] = '\0';
	fnode->uid   = 0;
	fnode->gid   = 0;
	fnode->flags = 0;
	fnode->read  = read_pipe;
	fnode->write = write_pipe;
	fnode->open  = open_pipe;
	fnode->close = close_pipe;
	fnode->readdir = NULL;
	fnode->finddir = NULL;

	fnode->inode = (uintptr_t)pipe;

	pipe->buffer    = buffer;
	pipe->write_ptr = 0;
	pipe->read_ptr  = 0;
	pipe->size      = size;
	pipe->refcount  = 0;
	pipe->lock      = 0;

	return fnode;
}
