/**
 * @file kernel/vfs/pipe.c
 * @brief Legacy buffered pipe, used for char devices.
 *
 * This is the legacy pipe implementation. If you are looking for
 * the userspace pipes, @ref read_unixpipe.
 *
 * This implements a simple one-direction buffer suitable for use
 * by, eg., device drivers that want to offer a character-driven
 * interface to userspace without having to worry too much about
 * timing or getting blocked.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2021 K. Lange
 */

#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/printf.h>
#include <kernel/vfs.h>
#include <kernel/pipe.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/spinlock.h>
#include <kernel/signal.h>
#include <kernel/time.h>

#include <sys/signal_defs.h>

#define DEBUG_PIPES 0

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
	spin_lock(pipe->ptr_lock);
	int out = pipe_unread(pipe);
	spin_unlock(pipe->ptr_lock);
	return out;
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

int pipe_unsize(fs_node_t * node) {
	pipe_device_t * pipe = (pipe_device_t *)node->device;
	spin_lock(pipe->ptr_lock);
	int out = pipe_available(pipe);
	spin_unlock(pipe->ptr_lock);
	return out;
}

static inline void pipe_increment_read(pipe_device_t * pipe) {
	spin_lock(pipe->ptr_lock);
	pipe->read_ptr++;
	if (pipe->read_ptr == pipe->size) {
		pipe->read_ptr = 0;
	}
	spin_unlock(pipe->ptr_lock);
}

static inline void pipe_increment_write(pipe_device_t * pipe) {
	spin_lock(pipe->ptr_lock);
	pipe->write_ptr++;
	if (pipe->write_ptr == pipe->size) {
		pipe->write_ptr = 0;
	}
	spin_unlock(pipe->ptr_lock);
}

static inline void pipe_increment_write_by(pipe_device_t * pipe, size_t amount) {
	pipe->write_ptr = (pipe->write_ptr + amount) % pipe->size;
}

static void pipe_alert_waiters(pipe_device_t * pipe) {
	spin_lock(pipe->alert_lock);
	while (pipe->alert_waiters->head) {
		node_t * node = list_dequeue(pipe->alert_waiters);
		process_t * p = node->value;
		free(node);
		spin_unlock(pipe->alert_lock);

		process_alert_node(p, pipe);

		spin_lock(pipe->alert_lock);
	}
	spin_unlock(pipe->alert_lock);
}

ssize_t read_pipe(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->device;

	if (pipe->dead) {
		send_signal(this_core->current_process->id, SIGPIPE, 1);
		return 0;
	}

	size_t collected = 0;
	while (collected == 0) {
		spin_lock(pipe->lock_read);
		if (pipe_unread(pipe) >= size) {
			while (pipe_unread(pipe) > 0 && collected < size) {
				buffer[collected] = pipe->buffer[pipe->read_ptr];
				pipe_increment_read(pipe);
				collected++;
			}
		}
		wakeup_queue(pipe->wait_queue_writers);
		/* Deschedule and switch */
		if (collected == 0) {
			if (sleep_on_unlocking(pipe->wait_queue_readers, &pipe->lock_read)) {
				if (!collected) return -ERESTARTSYS;
				break;
			}
		} else {
			spin_unlock(pipe->lock_read);
		}
	}

	return collected;
}

ssize_t write_pipe(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->device;

	if (pipe->dead) {
		send_signal(this_core->current_process->id, SIGPIPE, 1);
		return 0;
	}

	size_t written = 0;
	while (written < size) {
		spin_lock(pipe->lock_read);
		/* These pipes enforce atomic writes, poorly. */
		if (pipe_available(pipe) > size) {
			while (pipe_available(pipe) > 0 && written < size) {
				pipe->buffer[pipe->write_ptr] = buffer[written];
				pipe_increment_write(pipe);
				written++;
			}
		}
		wakeup_queue(pipe->wait_queue_readers);
		pipe_alert_waiters(pipe);
		if (written < size) {
			if (sleep_on_unlocking(pipe->wait_queue_writers, &pipe->lock_read)) {
				if (!written) return -ERESTARTSYS;
				break;
			}
		} else {
			spin_unlock(pipe->lock_read);
		}
	}

	return written;
}

void open_pipe(fs_node_t * node, unsigned int flags) {
	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->device;

	/* Add a reference */
	pipe->refcount++;

	return;
}

void close_pipe(fs_node_t * node) {
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

static int pipe_check(fs_node_t * node) {
	pipe_device_t * pipe = (pipe_device_t *)node->device;

	if (pipe_unread(pipe) > 0) {
		return 0;
	}

	return 1;
}

static int pipe_wait(fs_node_t * node, void * process) {
	pipe_device_t * pipe = (pipe_device_t *)node->device;

	spin_lock(pipe->alert_lock);
	if (!list_find(pipe->alert_waiters, process)) {
		list_insert(pipe->alert_waiters, process);
	}
	spin_unlock(pipe->alert_lock);

	spin_lock(pipe->wait_lock);
	list_insert(((process_t *)process)->node_waits, pipe);
	spin_unlock(pipe->wait_lock);

	return 0;
}

void pipe_destroy(fs_node_t * node) {
	pipe_device_t * pipe = (pipe_device_t *)node->device;
	spin_lock(pipe->ptr_lock);
	pipe->dead = 1;
	pipe_alert_waiters(pipe);
	wakeup_queue(pipe->wait_queue_writers);
	wakeup_queue(pipe->wait_queue_readers);
	free(pipe->alert_waiters);
	free(pipe->wait_queue_writers);
	free(pipe->wait_queue_readers);
	free(pipe->buffer);
	spin_unlock(pipe->ptr_lock);
	free(pipe);
}

fs_node_t * make_pipe(size_t size) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	pipe_device_t * pipe = malloc(sizeof(pipe_device_t));
	memset(fnode, 0, sizeof(fs_node_t));
	memset(pipe, 0, sizeof(pipe_device_t));

	fnode->device = 0;
	fnode->name[0] = '\0';
	snprintf(fnode->name, 100, "[pipe]");
	fnode->uid   = 0;
	fnode->gid   = 0;
	fnode->mask  = 0666;
	fnode->flags = FS_PIPE;
	fnode->read  = read_pipe;
	fnode->write = write_pipe;
	fnode->open  = open_pipe;
	fnode->close = close_pipe;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL; /* TODO ioctls for pipes? maybe */
	fnode->get_size = pipe_size;

	fnode->selectcheck = pipe_check;
	fnode->selectwait  = pipe_wait;

	fnode->atime = now();
	fnode->mtime = fnode->atime;
	fnode->ctime = fnode->atime;

	fnode->device = pipe;

	pipe->buffer    = malloc(size);
	pipe->write_ptr = 0;
	pipe->read_ptr  = 0;
	pipe->size      = size;
	pipe->refcount  = 0;
	pipe->dead      = 0;

	spin_init(pipe->lock_read);
	spin_init(pipe->alert_lock);
	spin_init(pipe->wait_lock);
	spin_init(pipe->ptr_lock);

	pipe->wait_queue_writers = list_create("pipe writers",pipe);
	pipe->wait_queue_readers = list_create("pip readers",pipe);
	pipe->alert_waiters = list_create("pipe alert waiters",pipe);

	return fnode;
}
