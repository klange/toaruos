/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * Buffered Pipe
 */

#include <system.h>
#include <fs.h>
#include <pipe.h>
#include <logging.h>

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

int pipe_size(fs_node_t * node) {
	pipe_device_t * pipe = (pipe_device_t *)node->device;
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

static inline void pipe_increment_write_by(pipe_device_t * pipe, size_t amount) {
	pipe->write_ptr = (pipe->write_ptr + amount) % pipe->size;
}

uint32_t read_pipe(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	assert(node->device != 0 && "Attempted to read from a fully-closed pipe.");

	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->device;

#if DEBUG_PIPES
	if (pipe->size > 300) { /* Ignore small pipes (ie, keyboard) */
		kprintf("[debug] Call to read from pipe 0x%x\n", node->device);
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
		spin_lock(&pipe->lock);
		while (pipe_unread(pipe) > 0 && collected < size) {
			buffer[collected] = pipe->buffer[pipe->read_ptr];
			pipe_increment_read(pipe);
			collected++;
		}
		spin_unlock(&pipe->lock);
		wakeup_queue(pipe->wait_queue);
		//switch_from_cross_thread_lock();
		/* Deschedule and switch */
		if (collected == 0) {
			sleep_on(pipe->wait_queue);
		}
	}

	return collected;
}

uint32_t write_pipe(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	assert(node->device != 0 && "Attempted to write to a fully-closed pipe.");

	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->device;

#if DEBUG_PIPES
	if (pipe->size > 300) { /* Ignore small pipes (ie, keyboard) */
		kprintf("[debug] Call to write to pipe 0x%x\n", node->device);
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
		spin_lock(&pipe->lock);

#if 0
		size_t available = 0;
		if (pipe->read_ptr <= pipe->write_ptr) {
			available = pipe->size - pipe->write_ptr;
		} else {
			available = pipe->read_ptr - pipe->write_ptr - 1;
		}
		if (available) {
			available = min(available, size - written);
			memcpy(&pipe->buffer[pipe->write_ptr], buffer, available);
			pipe_increment_write_by(pipe, available);
			written += available;
		}
#else
		while (pipe_available(pipe) > 0 && written < size) {
			pipe->buffer[pipe->write_ptr] = buffer[written];
			pipe_increment_write(pipe);
			written++;
		}
#endif

		spin_unlock(&pipe->lock);
		wakeup_queue(pipe->wait_queue);
		if (written < size) {
			sleep_on(pipe->wait_queue);
		}
	}

	return written;
}

void open_pipe(fs_node_t * node, uint8_t read, uint8_t write) {
	assert(node->device != 0 && "Attempted to open a fully-closed pipe.");

	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->device;

	/* Add a reference */
	pipe->refcount++;

	return;
}

void close_pipe(fs_node_t * node) {
	assert(node->device != 0 && "Attempted to close an already fully-closed pipe.");

	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->device;

	/* Drop one reference */
	pipe->refcount--;

	/* Check the reference count number */
	if (pipe->refcount == 0) {
#if 0
		/* No other references exist, free the pipe (but not its buffer) */
		free(pipe->buffer);
		list_free(pipe->wait_queue);
		free(pipe->wait_queue);
		free(pipe);
		/* And let the creator know there are no more references */
		node->device = 0;
#endif
	}

	return;
}

fs_node_t * make_pipe(size_t size) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	pipe_device_t * pipe = malloc(sizeof(pipe_device_t));

	fnode->device = 0;
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
	fnode->ioctl   = NULL; /* TODO ioctls for pipes? maybe */
	fnode->get_size = pipe_size;

	fnode->atime = now();
	fnode->mtime = fnode->atime;
	fnode->ctime = fnode->atime;

	fnode->device = pipe;

	pipe->buffer    = malloc(size);
	pipe->write_ptr = 0;
	pipe->read_ptr  = 0;
	pipe->size      = size;
	pipe->refcount  = 0;
	pipe->lock      = 0;

	pipe->wait_queue = list_create();

	return fnode;
}
