#pragma once

#include <stddef.h>
#include <kernel/vfs.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>

typedef struct _pipe_device {
	uint8_t * buffer;
	size_t write_ptr;
	size_t read_ptr;
	size_t size;
	size_t refcount;
	list_t * wait_queue_readers;
	list_t * wait_queue_writers;
	int dead;
	list_t * alert_waiters;

	spin_lock_t lock_read;
	spin_lock_t lock_write;
	spin_lock_t alert_lock;
	spin_lock_t wait_lock;
	spin_lock_t ptr_lock;
} pipe_device_t;

fs_node_t * make_pipe(size_t size);
int pipe_size(fs_node_t * node);
int pipe_unsize(fs_node_t * node);

