#ifndef RING_BUFFER_H
#define RING_BUFFER_H

typedef struct {
	unsigned char * buffer;
	size_t write_ptr;
	size_t read_ptr;
	size_t size;
	uint8_t volatile lock;
	list_t * wait_queue;
} ring_buffer_t;

size_t ring_buffer_unread(ring_buffer_t * ring_buffer);
size_t ring_buffer_size(fs_node_t * node);
size_t ring_buffer_available(ring_buffer_t * ring_buffer);
size_t ring_buffer_read(ring_buffer_t * ring_buffer, size_t size, uint8_t * buffer);
size_t ring_buffer_write(ring_buffer_t * ring_buffer, size_t size, uint8_t * buffer);

ring_buffer_t * ring_buffer_create(size_t size);

#endif
