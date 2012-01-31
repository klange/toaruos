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
	uint8_t volatile lock;
} pipe_device_t;

fs_node_t * make_pipe(size_t size);
size_t pipe_size(fs_node_t * node);

#endif
