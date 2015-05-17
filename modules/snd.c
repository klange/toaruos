/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Mike Gerow
 *
 * Sound subsystem.
 *
 * Currently has the ability to mix several sound sources together. Could use
 * a /dev/mixer device to allow changing of audio settings. Also could use
 * the ability to change frequency and format for audio samples. Also doesn't
 * really support multiple devices despite the interface suggesting it might...
 */

#include <mod/snd.h>

#include <list.h>
#include <mod/shell.h>
#include <module.h>
#include <ringbuffer.h>
#include <system.h>

/* Utility macros */
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#define SND_BUF_SIZE 0x1000

static uint32_t snd_write(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer);
static int snd_ioctl(fs_node_t * node, int request, void * argp);
static void snd_open(fs_node_t * node, unsigned int flags);
static void snd_close(fs_node_t * node);

static uint8_t  _devices_lock;
static list_t _devices; 
static fs_node_t _main_fnode = {
	.name   = "dsp",
	.device = &_devices,
	.flags  = FS_CHARDEVICE,
	.ioctl  = snd_ioctl,
	.write  = snd_write,
	.open   = snd_open,
	.close  = snd_close,
};
static uint8_t _buffers_lock;
static list_t _buffers;

int snd_register(snd_device_t * device) {
	int rv = 0;

	debug_print(WARNING, "[snd] _devices lock: %d", _devices_lock);
	spin_lock(&_devices_lock);
	if (list_find(&_devices, device)) {
		debug_print(WARNING, "[snd] attempt to register duplicate %s", device->name);
		rv = -1;
		goto snd_register_cleanup;
	}
	list_insert(&_devices, device);
	debug_print(NOTICE, "[snd] %s registered", device->name);

snd_register_cleanup:
	spin_unlock(&_devices_lock);
	return rv;
}

int snd_unregister(snd_device_t * device) {
	int rv = 0;

	node_t * node = list_find(&_devices, device);
	if (!node) {
		debug_print(WARNING, "[snd] attempted to unregister %s, "
				"but it was never registered", device->name);
		goto snd_unregister_cleanup;
	}
	list_delete(&_devices, node);
	debug_print(NOTICE, "[snd] %s unregistered", device->name);

snd_unregister_cleanup:
	spin_unlock(&_devices_lock);
	return rv;
}

static uint32_t snd_write(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return ring_buffer_write(node->device, size, buffer);
}

static int snd_ioctl(fs_node_t * node, int request, void * argp) {
	/* Potentially use this to set sample rates in the future */
	return -1;
}

static void snd_open(fs_node_t * node, unsigned int flags) {
	/* 
	 * XXX(gerow): A process could take the memory of the entire system by opening
	 * too many of these...
	 */
	/* Allocate a buffer for the node and keep a reference for ourselves */
	node->device = ring_buffer_create(SND_BUF_SIZE);
	spin_lock(&_buffers_lock);
	list_insert(&_buffers, node->device);
	spin_unlock(&_buffers_lock);
}

static void snd_close(fs_node_t * node) {
	spin_lock(&_buffers_lock);
	list_delete(&_buffers, list_find(&_buffers, node->device));
	spin_unlock(&_buffers_lock);
}

int snd_request_buf(snd_device_t * device, uint32_t size, uint8_t *buffer) {
	static uint8_t tmp_buf[0x100];

	memset(buffer, 0, size);

	spin_lock(&_buffers_lock);
	foreach(buf_node, &_buffers) {
		ring_buffer_t * buf = buf_node->value;
		/* ~0x3 is to ensure we don't read partial samples or just a single channel */
		size_t read_size = MIN(ring_buffer_unread(buf) & ~0x3, size);
		size_t bytes_left = read_size;
		uint8_t * adding_ptr = buffer;
		while (bytes_left) {
			size_t this_read_size = MIN(bytes_left, sizeof(tmp_buf));
			ring_buffer_read(buf, this_read_size, tmp_buf);
			int16_t * ducking_ptr = (int16_t *)tmp_buf;
			/*
			 * Reduce the sample by a half so that multiple sources won't immediately
			 * cause awful clipping. This is kind of a hack since it would probably be
			 * better to just use some kind of compressor.
			 */
			for (size_t i = 0; i < sizeof(tmp_buf) / sizeof(*ducking_ptr); i++) {
				ducking_ptr[i] /= 2;
			}
			for (size_t i = 0; i < this_read_size; i++) {
				adding_ptr[i] += tmp_buf[i];
			}
			adding_ptr += this_read_size;
			bytes_left -= this_read_size;
		}
	}
	spin_unlock(&_buffers_lock);

	return size;
}

static snd_device_t * snd_main_device() {
	spin_lock(&_devices_lock);
	foreach(node, &_devices) {
		spin_unlock(&_devices_lock);
		return node->value;
	}

	spin_unlock(&_devices_lock);
	return NULL;
}

DEFINE_SHELL_FUNCTION(snd_full, "[debug] turn snd master to full") {
	snd_main_device()->mixer_write(SND_KNOB_MASTER, UINT32_MAX);

	return 0;
}

DEFINE_SHELL_FUNCTION(snd_half, "[debug] turn snd master to half") {
	snd_main_device()->mixer_write(SND_KNOB_MASTER, UINT32_MAX / 2);

	return 0;
}

DEFINE_SHELL_FUNCTION(snd_off, "[debug] turn snd master to lowest volume") {
	snd_main_device()->mixer_write(SND_KNOB_MASTER, 0);

	return 0;
}

static int init(void) {
	vfs_mount("/dev/dsp", &_main_fnode);

	BIND_SHELL_FUNCTION(snd_full);
	BIND_SHELL_FUNCTION(snd_half);
	BIND_SHELL_FUNCTION(snd_off);
	return 0;
}

static int fini(void) {
	/* umount? */
	return 0;
}

MODULE_DEF(snd, init, fini);
MODULE_DEPENDS(debugshell);
