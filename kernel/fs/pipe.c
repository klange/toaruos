/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * Buffered Pipe
 */

#include <system.h>
#include <fs.h>
#include <pipe.h>

#define DEBUG_PIPES 0

uint32_t read_pipe(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_pipe(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_pipe(fs_node_t *node, uint8_t read, uint8_t write);
void close_pipe(fs_node_t *node);

static inline size_t pipe_unread(pipe_device_t * pipe) {
	if (pipe->read_ptr == pipe->write_ptr) {
		return 0;
	}
	if (pipe->read_ptr > pipe->write_ptr) {
		return (pipe->size - pipe->read_ptr) + pipe->write_ptr;
	} else {
		return (pipe->write_ptr - pipe->read_ptr);
	}
}

size_t pipe_size(fs_node_t * node) {
	pipe_device_t * pipe = (pipe_device_t *)node->inode;
	return pipe_unread(pipe);
}

static inline size_t pipe_available(pipe_device_t * pipe) {
	if (pipe->read_ptr == pipe->write_ptr) {
		return pipe->size - 1;
	}

	if (pipe->read_ptr > pipe->write_ptr) {
		return pipe->read_ptr - pipe->write_ptr - 1;
	} else {
		return (pipe->size - pipe->write_ptr) + pipe->read_ptr - 1;
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

#if DEBUG_PIPES
	if (pipe->size > 300) { /* Ignore small pipes (ie, keyboard) */
		kprintf("[debug] Call to read from pipe 0x%x\n", node->inode);
		kprintf("        Unread bytes:    %d\n", pipe_unread(pipe));
		kprintf("        Total size:      %d\n", pipe->size);
		kprintf("        Request size:    %d\n", size);
		kprintf("        Write pointer:   %d\n", pipe->write_ptr);
		kprintf("        Read  pointer:   %d\n", pipe->read_ptr);
		kprintf("        Buffer address:  0x%x\n", pipe->buffer);
	}
#endif

	size_t collected = 0;
	while (collected == 0) {
		while (pipe_unread(pipe) > 0 && collected < size) {
			spin_lock(&pipe->lock);
			buffer[collected] = pipe->buffer[pipe->read_ptr];
			pipe_increment_read(pipe);
			spin_unlock(&pipe->lock);
			if (buffer[collected] == '\n') {
				return collected + 1;
			}
			collected++;
			wakeup_queue(pipe->wait_queue);
		}
		//switch_from_cross_thread_lock();
		/* Deschedule and switch */
		if (collected == 0) {
			sleep_on(pipe->wait_queue);
		}
	}

	return collected;
}

uint32_t write_pipe(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	assert(node->inode != 0 && "Attempted to write to a fully-closed pipe.");

	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->inode;

#if DEBUG_PIPES
	if (pipe->size > 300) { /* Ignore small pipes (ie, keyboard) */
		kprintf("[debug] Call to write to pipe 0x%x\n", node->inode);
		kprintf("        Available space: %d\n", pipe_available(pipe));
		kprintf("        Total size:      %d\n", pipe->size);
		kprintf("        Request size:    %d\n", size);
		kprintf("        Write pointer:   %d\n", pipe->write_ptr);
		kprintf("        Read  pointer:   %d\n", pipe->read_ptr);
		kprintf("        Buffer address:  0x%x\n", pipe->buffer);
		kprintf(" Write: %s\n", buffer);
	}
#endif

	size_t written = 0;
	while (written < size) {
		while (pipe_available(pipe) > 0 && written < size) {
			spin_lock(&pipe->lock);
			pipe->buffer[pipe->write_ptr] = buffer[written];
			pipe_increment_write(pipe);
			spin_unlock(&pipe->lock);
			written++;
			wakeup_queue(pipe->wait_queue);
		}
		if (written < size) {
			sleep_on(pipe->wait_queue);
		}
	}

	return written;
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
		free(pipe->buffer);
		list_free(pipe->wait_queue);
		free(pipe->wait_queue);
		free(pipe);
		/* And let the creator know there are no more references */
		node->inode = 0;
	}

	return;
}

fs_node_t * make_pipe(size_t size) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	pipe_device_t * pipe = malloc(sizeof(fs_node_t));

	fnode->inode = 0;
	fnode->name[0] = '\0';
	sprintf(fnode->name, "[pipe]");
	fnode->uid   = 0;
	fnode->gid   = 0;
	fnode->flags = FS_PIPE;
	fnode->read  = read_pipe;
	fnode->write = write_pipe;
	fnode->open  = open_pipe;
	fnode->close = close_pipe;
	fnode->readdir = NULL;
	fnode->finddir = NULL;

	fnode->inode = (uintptr_t)pipe;

	pipe->buffer    = malloc(size);
	pipe->write_ptr = 0;
	pipe->read_ptr  = 0;
	pipe->size      = size;
	pipe->refcount  = 0;
	pipe->lock      = 0;

	pipe->wait_queue = list_create();

	return fnode;
}
