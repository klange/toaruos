/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Pipe
 */

#ifndef PIPE_H
#define PIPE_H

#include <types.h>

typedef struct _pipe_device {
	uint8_t * buffer;
	size_t write_ptr;
	size_t read_ptr;
	size_t size;
	size_t refcount;
	volatile int lock_read[2];
	volatile int lock_write[2];
	list_t * wait_queue_readers;
	list_t * wait_queue_writers;
	int dead;
} pipe_device_t;

fs_node_t * make_pipe(size_t size);
int pipe_size(fs_node_t * node);
int pipe_unsize(fs_node_t * node);

#endif
