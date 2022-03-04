/**
 * @file kernel/vfs/unixpipe.c
 * @brief Implementation of Unix pipes.
 *
 * Provides for unidirectional communication between processes.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/printf.h>
#include <kernel/pipe.h>
#include <kernel/string.h>
#include <kernel/ringbuffer.h>
#include <kernel/process.h>
#include <kernel/signal.h>

#include <sys/signal_defs.h>
#include <sys/ioctl.h>

#define UNIX_PIPE_BUFFER 4096

struct unix_pipe {
	fs_node_t * read_end;
	fs_node_t * write_end;

	volatile int read_closed;
	volatile int write_closed;

	ring_buffer_t * buffer;
};

static void close_complete(struct unix_pipe * self) {
	ring_buffer_destroy(self->buffer);
}

static ssize_t read_unixpipe(fs_node_t * node, off_t offset, size_t size, uint8_t *buffer) {
	struct unix_pipe * self = node->device;
	if (self->write_closed && !ring_buffer_unread(self->buffer)) {
		return 0;
	}
	return ring_buffer_read(self->buffer, size, buffer);
}

static ssize_t write_unixpipe(fs_node_t * node, off_t offset, size_t size, uint8_t *buffer) {
	struct unix_pipe * self = node->device;
	if (self->read_closed) {
		send_signal(this_core->current_process->id, SIGPIPE, 1);
		return -EPIPE;
	}
	return ring_buffer_write(self->buffer, size, buffer);
}

static void close_read_pipe(fs_node_t * node) {
	struct unix_pipe * self = node->device;

	spin_lock(self->buffer->lock);
	self->read_closed = 1;
	if (!self->write_closed) {
		ring_buffer_interrupt(self->buffer);
	}
	spin_unlock(self->buffer->lock);
}

static void close_write_pipe(fs_node_t * node) {
	struct unix_pipe * self = node->device;

	spin_lock(self->buffer->lock);
	self->write_closed = 1;
	if (!self->read_closed) {
		ring_buffer_interrupt(self->buffer);
		if (!ring_buffer_unread(self->buffer)) {
			ring_buffer_alert_waiters(self->buffer);
		}
	}
	spin_unlock(self->buffer->lock);
}

static int check_pipe(fs_node_t * node) {
	struct unix_pipe * self = node->device;
	if (ring_buffer_unread(self->buffer) > 0) {
		return 0;
	}
	if (self->write_closed) return 0;
	return 1;
}

static int wait_pipe(fs_node_t * node, void * process) {
	struct unix_pipe * self = node->device;
	ring_buffer_select_wait(self->buffer, process);
	return 0;
}


int make_unix_pipe(fs_node_t ** pipes) {
	size_t size = UNIX_PIPE_BUFFER;

	pipes[0] = malloc(sizeof(fs_node_t));
	pipes[1] = malloc(sizeof(fs_node_t));

	memset(pipes[0], 0, sizeof(fs_node_t));
	memset(pipes[1], 0, sizeof(fs_node_t));

	snprintf(pipes[0]->name, 100, "[pipe:read]");
	snprintf(pipes[1]->name, 100, "[pipe:write]");

	pipes[0]->mask = 0666;
	pipes[1]->mask = 0666;

	pipes[0]->flags = FS_PIPE;
	pipes[1]->flags = FS_PIPE;

	pipes[0]->read = read_unixpipe;
	pipes[1]->write = write_unixpipe;

	pipes[0]->close = close_read_pipe;
	pipes[1]->close = close_write_pipe;

	/* Read end can wait */
	pipes[0]->selectcheck = check_pipe;
	pipes[0]->selectwait = wait_pipe;

	struct unix_pipe * internals = malloc(sizeof(struct unix_pipe));
	internals->read_end = pipes[0];
	internals->write_end = pipes[1];
	internals->read_closed = 0;
	internals->write_closed = 0;
	internals->buffer = ring_buffer_create(size);

	pipes[0]->device = internals;
	pipes[1]->device = internals;

	return 0;
}
