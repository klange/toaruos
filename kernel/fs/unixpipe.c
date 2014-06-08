/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <fs.h>
#include <pipe.h>
#include <logging.h>
#include <printf.h>

#include <ioctl.h>
#include <ringbuffer.h>

#define UNIX_PIPE_BUFFER 512

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

static uint32_t read_unixpipe(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	struct unix_pipe * self = node->device;
	size_t read = 0;

	while (read < size) {
		if (self->write_closed && !ring_buffer_unread(self->buffer)) {
			return read;
		}
		size_t r = ring_buffer_read(self->buffer, 1, buffer+read);
		if (r && *((char *)(buffer + read)) == '\n') {
			return read+r;
		}
		read += r;
	}

	return read;
}

static uint32_t write_unixpipe(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	struct unix_pipe * self = node->device;
	size_t written = 0;

	while (written < size) {
		if (self->read_closed) {
			/* SIGPIPE to current process */
			signal_t * sig = malloc(sizeof(signal_t));
			sig->handler = current_process->signals.functions[SIGPIPE];
			sig->signum  = SIGPIPE;
			handle_signal((process_t *)current_process, sig);

			return written;
		}
		size_t w = ring_buffer_write(self->buffer, 1, buffer+written);
		written += w;
	}

	return written;
}

static void close_read_pipe(fs_node_t * node) {
	struct unix_pipe * self = node->device;

	debug_print(NOTICE, "Closing read end of pipe.");

	self->read_closed = 1;
	if (self->write_closed) {
		debug_print(NOTICE, "Both ends now closed, should clean up.");
	} else {
		ring_buffer_interrupt(self->buffer);
	}
}

static void close_write_pipe(fs_node_t * node) {
	struct unix_pipe * self = node->device;

	debug_print(NOTICE, "Closing write end of pipe.");

	self->write_closed = 1;
	if (self->read_closed) {
		debug_print(NOTICE, "Both ends now closed, should clean up.");
	} else {
		ring_buffer_interrupt(self->buffer);
	}
}

int make_unix_pipe(fs_node_t ** pipes) {
	size_t size = UNIX_PIPE_BUFFER;

	pipes[0] = malloc(sizeof(fs_node_t));
	pipes[1] = malloc(sizeof(fs_node_t));

	memset(pipes[0], 0, sizeof(fs_node_t));
	memset(pipes[1], 0, sizeof(fs_node_t));

	sprintf(pipes[0]->name, "[pipe:read]");
	sprintf(pipes[1]->name, "[pipe:write]");

	pipes[0]->flags = FS_PIPE;
	pipes[1]->flags = FS_PIPE;

	pipes[0]->read = read_unixpipe;
	pipes[1]->write = write_unixpipe;

	pipes[0]->close = close_read_pipe;
	pipes[1]->close = close_write_pipe;

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
