/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
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
		spin_lock(ring_buffer->lock);
		while (ring_buffer_unread(ring_buffer) > 0 && collected < size) {
			buffer[collected] = ring_buffer->buffer[ring_buffer->read_ptr];
			ring_buffer_increment_read(ring_buffer);
			collected++;
		}
		spin_unlock(ring_buffer->lock);
		wakeup_queue(ring_buffer->wait_queue_writers);
		if (collected == 0) {
			if (sleep_on(ring_buffer->wait_queue_readers) && ring_buffer->internal_stop) {
				ring_buffer->internal_stop = 0;
				break;
			}
		}
	}
	wakeup_queue(ring_buffer->wait_queue_writers);
	return collected;
}

size_t ring_buffer_write(ring_buffer_t * ring_buffer, size_t size, uint8_t * buffer) {
	size_t written = 0;
	while (written < size) {
		spin_lock(ring_buffer->lock);

		while (ring_buffer_available(ring_buffer) > 0 && written < size) {
			ring_buffer->buffer[ring_buffer->write_ptr] = buffer[written];
			ring_buffer_increment_write(ring_buffer);
			written++;
		}

		spin_unlock(ring_buffer->lock);
		wakeup_queue(ring_buffer->wait_queue_readers);
		if (written < size) {
			if (sleep_on(ring_buffer->wait_queue_writers) && ring_buffer->internal_stop) {
				ring_buffer->internal_stop = 0;
				break;
			}
		}
	}

	wakeup_queue(ring_buffer->wait_queue_readers);
	return written;
}

ring_buffer_t * ring_buffer_create(size_t size) {
	ring_buffer_t * out = malloc(sizeof(ring_buffer_t));

	out->buffer     = malloc(size);
	out->write_ptr  = 0;
	out->read_ptr   = 0;
	out->size       = size;

	spin_init(out->lock);

	out->internal_stop = 0;

	out->wait_queue_readers = list_create();
	out->wait_queue_writers = list_create();

	return out;
}

void ring_buffer_destroy(ring_buffer_t * ring_buffer) {
	free(ring_buffer->buffer);

	wakeup_queue(ring_buffer->wait_queue_writers);
	wakeup_queue(ring_buffer->wait_queue_readers);

	list_free(ring_buffer->wait_queue_writers);
	list_free(ring_buffer->wait_queue_readers);

	free(ring_buffer->wait_queue_writers);
	free(ring_buffer->wait_queue_readers);
}

void ring_buffer_interrupt(ring_buffer_t * ring_buffer) {
	ring_buffer->internal_stop = 1;
	wakeup_queue_interrupted(ring_buffer->wait_queue_readers);
	wakeup_queue_interrupted(ring_buffer->wait_queue_writers);
}

