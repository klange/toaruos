/**
 * @file  kernel/vfs/console.c
 * @brief Device file interface to the kernel console.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdarg.h>
#include <errno.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/time.h>

static fs_node_t * console_dev = NULL;

/** Things we use to determine if the clock is ready. */
extern uint64_t arch_boot_time;

/** Things we use for framebuffer output. */
extern uint16_t lfb_resolution_x;
extern size_t (*printf_output)(size_t, uint8_t *);

static size_t (*console_write)(size_t, uint8_t *) = NULL;

static uint8_t tmp_buffer[4096] __attribute__((aligned(4096)));
static uint8_t * buffer_start = tmp_buffer;

static ssize_t write_console(size_t size, uint8_t *buffer) {
	if (console_write) return console_write(size,buffer);

	if (buffer_start + size >= tmp_buffer + sizeof(tmp_buffer)) {
		return 0; /* uh oh */
	}

	memcpy(buffer_start, buffer, size);
	buffer_start += size;

	return size;
}

struct dprintf_data {
	int prev_was_lf;
	int left_width;
};

static int cb_printf(void * user, char c) {
	struct dprintf_data * data = user;
	if (data->prev_was_lf) {
		for (int i = 0; i < data->left_width; ++i) write_console(1, (uint8_t*)" ");
		data->prev_was_lf = 0;
	}
	if (c == '\n') data->prev_was_lf = 1;
	write_console(1, (uint8_t*)&c);
	return 0;
}

void console_set_output(size_t (*output)(size_t,uint8_t*)) {
	console_write = output;

	if (buffer_start != tmp_buffer) {
		console_write(buffer_start - tmp_buffer, tmp_buffer);
		buffer_start = tmp_buffer;
	}
}

int dprintf(const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);

	/* Is this a FATAL message? */

	/* If it's ready now but wasn't ready previously, are there
	 * things in the queue to dump? */

	/* Is this a fresh message for this core that we need to assign a timestamp to? */


	struct dprintf_data _data = {0,0};

	if (*fmt == '\a') {
		fmt++;
	} else {
		char timestamp[32];
		unsigned long timer_ticks, timer_subticks;
		relative_time(0,0,&timer_ticks,&timer_subticks);
		size_t ts_len = snprintf(timestamp, 31, "[%5lu.%06lu] ", timer_ticks, timer_subticks);
		_data.left_width = ts_len;
		write_console(ts_len, (uint8_t*)timestamp);
	}

	int out = xvasprintf(cb_printf, &_data, fmt, args);
	va_end(args);
	return out;
}

static ssize_t write_fs_console(fs_node_t * node, off_t offset, size_t size, uint8_t * buffer) {
	if (size > 0x1000) return -EINVAL;
	size_t size_in = size;
	if (size && *buffer == '\r') {
		write_console(1,(uint8_t*)"\r");
		buffer++;
		size--;
	}
	if (size) dprintf("%*s", (unsigned int)size, buffer);
	return size_in;
}

static fs_node_t * console_device_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "console");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask = 0660;
	fnode->flags   = FS_CHARDEVICE;
	fnode->write   = write_fs_console;
	return fnode;
}

void console_initialize(void) {
	console_dev = console_device_create();
	vfs_mount("/dev/console", console_dev);
}
