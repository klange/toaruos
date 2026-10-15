/**
 * @file kernel/misc/ringbuffer.c
 * @brief Generic ringbuffer with blocking reads and writes.
 *
 * Provides a buffer interface for devices such as at PTYs with
 * blocking reads and writes.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2021 K. Lange
 */
#include <stdint.h>
#include <stddef.h>
#include <bits/errno.h>
#include <bits/poll.h>
#include <kernel/types.h>
#include <kernel/ringbuffer.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>
#include <kernel/mmu.h>
#include <kernel/signal.h>

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

void ring_buffer_select_wait(ring_buffer_t * ring_buffer, void * process) {
	add_poll_wait(process, &ring_buffer->rd_queue);
}

void ring_buffer_discard(ring_buffer_t * ring_buffer) {
	spin_lock(ring_buffer->lock);
	ring_buffer->read_ptr = ring_buffer->write_ptr;
	spin_unlock(ring_buffer->lock);
}

ssize_t ring_buffer_read(ring_buffer_t * ring_buffer, size_t size, uint8_t * buffer) {
	size_t collected = 0;
	while (collected == 0) {
		spin_lock(ring_buffer->lock);
		while (ring_buffer_unread(ring_buffer) > 0 && collected < size) {
			buffer[collected] = ring_buffer->buffer[ring_buffer->read_ptr];
			ring_buffer_increment_read(ring_buffer);
			collected++;
		}
		waitable_awaken_all(&ring_buffer->wr_queue, (void*)1);
		if (collected == 0) {
			if (ring_buffer->internal_stop || ring_buffer->soft_stop) {
				ring_buffer->soft_stop = 0;
				spin_unlock(ring_buffer->lock);
				return 0;
			}

			if (waitable_await_unlocking(&ring_buffer->rd_queue, &ring_buffer->lock)) {
				if (has_pending()) return -ERESTARTSYS;
			}
		} else {
			spin_unlock(ring_buffer->lock);
		}
	}
	waitable_awaken_all(&ring_buffer->wr_queue, (void*)1);
	return collected;
}

ssize_t ring_buffer_write(ring_buffer_t * ring_buffer, size_t size, uint8_t * buffer) {
	size_t written = 0;
	while (written < size) {
		spin_lock(ring_buffer->lock);

		while (ring_buffer_available(ring_buffer) > 0 && written < size) {
			ring_buffer->buffer[ring_buffer->write_ptr] = buffer[written];
			ring_buffer_increment_write(ring_buffer);
			written++;
		}

		waitable_awaken_all(&ring_buffer->rd_queue, (void*)1);

		if (written < size) {
			if (ring_buffer->discard) {
				spin_unlock(ring_buffer->lock);
				break;
			}
			if (waitable_await_unlocking(&ring_buffer->wr_queue, &ring_buffer->lock)) {
				if (has_pending()) {
					if (!written) return -ERESTARTSYS;
					break;
				}
			}
			if (ring_buffer->internal_stop) break;
		} else {
			spin_unlock(ring_buffer->lock);
		}
	}

	waitable_awaken_all(&ring_buffer->rd_queue, (void*)1);
	return written;
}

ring_buffer_t * ring_buffer_create(size_t size) {
	ring_buffer_t * out = malloc(sizeof(ring_buffer_t));

	if (size == 4096) {
		out->buffer = mmu_map_from_physical(mmu_allocate_a_frame() << 12);
	} else {
		out->buffer     = malloc(size);
	}
	out->write_ptr  = 0;
	out->read_ptr   = 0;
	out->size       = size;

	spin_init(out->lock);

	out->internal_stop = 0;
	out->discard = 0;
	out->soft_stop = 0;

	waitable_queue_init(&out->rd_queue);
	waitable_queue_init(&out->wr_queue);

	return out;
}

void ring_buffer_destroy(ring_buffer_t * ring_buffer) {
	if (ring_buffer->size == 4096) {
		mmu_frame_release((uintptr_t)ring_buffer->buffer & 0xFFFFFFFFF);
	} else {
		free(ring_buffer->buffer);
	}

	waitable_awaken_all(&ring_buffer->wr_queue, (void*)1);
	waitable_awaken_all(&ring_buffer->rd_queue, (void*)1);
}

void ring_buffer_interrupt(ring_buffer_t * ring_buffer) {
	ring_buffer->internal_stop = 1;
	waitable_awaken_all(&ring_buffer->rd_queue, (void*)1);
	waitable_awaken_all(&ring_buffer->wr_queue, (void*)1);
}

void ring_buffer_eof(ring_buffer_t * ring_buffer) {
	ring_buffer->soft_stop = 1;
	waitable_awaken_all(&ring_buffer->rd_queue, (void*)1);
	waitable_awaken_all(&ring_buffer->wr_queue, (void*)1);
}

int ring_buffer_poll(ring_buffer_t * ring_buffer, struct fs_file_description * fd, void * table) {
	int mask = 0;

	if (fd->flags & PROC_FD_MODE_READ) {
		if (ring_buffer_unread(ring_buffer) > 0) mask |= POLLIN;
		if (ring_buffer->internal_stop) mask |= POLLHUP;
		add_poll_wait(table, &ring_buffer->rd_queue);
	}

	if (fd->flags & PROC_FD_MODE_WRITE) {
		if (ring_buffer_available(ring_buffer) > 0) mask |= POLLOUT;
		add_poll_wait(table, &ring_buffer->wr_queue);
	}

	return mask;
}
