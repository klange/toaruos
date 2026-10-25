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

#include <bits/errno.h>
#include <bits/poll.h>
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

ssize_t pipe_size(fs_node_t * node) {
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

ssize_t pipe_unsize(fs_node_t * node) {
	pipe_device_t * pipe = (pipe_device_t *)node->device;
	spin_lock(pipe->ptr_lock);
	ssize_t out = pipe_available(pipe);
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

ssize_t read_pipe(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer, uint64_t flags) {
	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->device;

	if (pipe->dead) {
		siginfo_t cause = {.si_errno = EPIPE};
		raise_signal(SIGPIPE, &cause);
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
		waitable_awaken_all(&pipe->wr_queue, (void*)1);
		/* Deschedule and switch */
		if (collected == 0) {
			if (waitable_await_unlocking(&pipe->rd_queue, &pipe->lock_read)) {
				if (has_pending()) {
					if (!collected) return -ERESTARTSYS;
					break;
				}
			}
		} else {
			spin_unlock(pipe->lock_read);
		}
	}

	return collected;
}

ssize_t write_pipe(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer, uint64_t flags) {
	/* Retreive the pipe object associated with this file node */
	pipe_device_t * pipe = (pipe_device_t *)node->device;

	if (pipe->dead) {
		siginfo_t cause = {.si_errno = EPIPE};
		raise_signal(SIGPIPE, &cause);
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
		waitable_awaken_all(&pipe->rd_queue, (void*)1);
		if (written < size) {
			if (waitable_await_unlocking(&pipe->wr_queue, &pipe->lock_read)) {
				if (has_pending()) {
					if (!written) return -ERESTARTSYS;
					break;
				}
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
	extern int add_poll_wait(void *, struct waitable_queue *);
	add_poll_wait(process, &pipe->rd_queue);
	return 0;
}

static int pipe_poll(struct fs_file_description *fd, void * table) {
	pipe_device_t * pipe = (pipe_device_t *)fd->inode->device;
	int mask = 0;

	if (fd->flags & PROC_FD_MODE_READ) {
		if (pipe_unread(pipe) > 0) mask |= POLLIN;
		if (pipe->dead) mask |= POLLHUP;
		add_poll_wait(table, &pipe->rd_queue);
	}

	if (fd->flags & PROC_FD_MODE_WRITE) {
		if (pipe_available(pipe) > 0) mask |= POLLOUT;
		if (pipe->dead) mask |= POLLERR;
		add_poll_wait(table, &pipe->wr_queue);
	}

	return mask;
}

void pipe_destroy(fs_node_t * node) {
	pipe_device_t * pipe = (pipe_device_t *)node->device;
	spin_lock(pipe->ptr_lock);
	pipe->dead = 1;
	waitable_awaken_all(&pipe->wr_queue, (void*)1);
	waitable_awaken_all(&pipe->rd_queue, (void*)1);
	free(pipe->buffer);
	spin_unlock(pipe->ptr_lock);
	free(pipe);
}

static fs_vtable_t pipe_ops = {
	.read  = read_pipe,
	.write = write_pipe,
	.open  = open_pipe,
	.close = close_pipe,
	.get_size = pipe_size,
	.selectcheck = pipe_check,
	.selectwait  = pipe_wait,
	.poll = pipe_poll,
};

fs_node_t * make_pipe(size_t size) {
	fs_node_t * fnode = calloc(1, sizeof(fs_node_t));
	pipe_device_t * pipe = calloc(1, sizeof(pipe_device_t));

	fnode->device = 0;
	fnode->uid   = 0;
	fnode->gid   = 0;
	fnode->mask  = 0666;
	fnode->type  = INO_FIFO;
	fnode->ops   = &pipe_ops;

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

	waitable_queue_init(&pipe->rd_queue);
	waitable_queue_init(&pipe->wr_queue);

	return fnode;
}
