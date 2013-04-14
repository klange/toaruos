#include <system.h>
#include <ringbuffer.h>

size_t ring_buffer_unread(ring_buffer_t * ring_buffer) {
	if (ring_buffer->read_ptr == ring_buffer->write_ptr) {
		return 0;
	}
	if (ring_buffer->read_ptr > ring_buffer->write_ptr) {
		return (ring_buffer->size - ring_buffer->read_ptr) + ring_buffer->write_ptr;
	} else {
		return (ring_buffer->write_ptr - ring_buffer->read_ptr);
	}
}

size_t ring_buffer_size(fs_node_t * node) {
	ring_buffer_t * ring_buffer = (ring_buffer_t *)node->device;
	return ring_buffer_unread(ring_buffer);
}

size_t ring_buffer_available(ring_buffer_t * ring_buffer) {
	if (ring_buffer->read_ptr == ring_buffer->write_ptr) {
		return ring_buffer->size - 1;
	}

	if (ring_buffer->read_ptr > ring_buffer->write_ptr) {
		return ring_buffer->read_ptr - ring_buffer->write_ptr - 1;
	} else {
		return (ring_buffer->size - ring_buffer->write_ptr) + ring_buffer->read_ptr - 1;
	}
}

static inline void ring_buffer_increment_read(ring_buffer_t * ring_buffer) {
	ring_buffer->read_ptr++;
	if (ring_buffer->read_ptr == ring_buffer->size) {
		ring_buffer->read_ptr = 0;
	}
}

static inline void ring_buffer_increment_write(ring_buffer_t * ring_buffer) {
	ring_buffer->write_ptr++;
	if (ring_buffer->write_ptr == ring_buffer->size) {
		ring_buffer->write_ptr = 0;
	}
}

size_t ring_buffer_read(ring_buffer_t * ring_buffer, size_t size, uint8_t * buffer) {
	size_t collected = 0;
	while (collected == 0) {
		spin_lock(&ring_buffer->lock);
		while (ring_buffer_unread(ring_buffer) > 0 && collected < size) {
			buffer[collected] = ring_buffer->buffer[ring_buffer->read_ptr];
			ring_buffer_increment_read(ring_buffer);
			collected++;
		}
		spin_unlock(&ring_buffer->lock);
		if (collected == 0) {
			wakeup_queue(ring_buffer->wait_queue);
			sleep_on(ring_buffer->wait_queue);
		}
	}
	wakeup_queue(ring_buffer->wait_queue);
	return collected;
}

size_t ring_buffer_write(ring_buffer_t * ring_buffer, size_t size, uint8_t * buffer) {
	size_t written = 0;
	while (written < size) {
		spin_lock(&ring_buffer->lock);

		while (ring_buffer_available(ring_buffer) > 0 && written < size) {
			ring_buffer->buffer[ring_buffer->write_ptr] = buffer[written];
			ring_buffer_increment_write(ring_buffer);
			written++;
		}

		spin_unlock(&ring_buffer->lock);
		if (written < size) {
			wakeup_queue(ring_buffer->wait_queue);
			sleep_on(ring_buffer->wait_queue);
		}
	}

	wakeup_queue(ring_buffer->wait_queue);
	return written;
}

ring_buffer_t * ring_buffer_create(size_t size) {
	ring_buffer_t * out = malloc(sizeof(ring_buffer_t));

	out->buffer     = malloc(size);
	out->write_ptr  = 0;
	out->read_ptr   = 0;
	out->lock       = 0;
	out->wait_queue = list_create();
	out->size       = size;

	return out;
}
