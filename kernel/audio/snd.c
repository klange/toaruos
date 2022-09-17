/**
 * @file  kernel/audio/snd.c
 * @brief Gerow's Audio Subsystem for ToaruOS
 *
 * Simple generic mixer interface. Allows userspace to pipe audio data
 * to the kernel audio drivers and control volume knobs.
 *
 * Currently has the ability to mix several sound sources together. Could use
 * a /dev/mixer device to allow changing of audio settings. Also could use
 * the ability to change frequency and format for audio samples. Also doesn't
 * really support multiple devices despite the interface suggesting it might...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2021 K. Lange
 * Copyright (C) 2015 Mike Gerow
 */

#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/ringbuffer.h>
#include <kernel/list.h>
#include <kernel/printf.h>
#include <kernel/spinlock.h>

#include <kernel/mod/snd.h>
#include <errno.h>

/* Utility macros */
#define N_ELEMENTS(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define SND_BUF_SIZE 0x4000

static ssize_t snd_dsp_write(fs_node_t * node, off_t offset, size_t size, uint8_t *buffer);
static int snd_dsp_ioctl(fs_node_t * node, unsigned long request, void * argp);
static void snd_dsp_open(fs_node_t * node, unsigned int flags);
static void snd_dsp_close(fs_node_t * node);

static int snd_mixer_ioctl(fs_node_t * node, unsigned long request, void * argp);
static void snd_mixer_open(fs_node_t * node, unsigned int flags);
static void snd_mixer_close(fs_node_t * node);

static spin_lock_t _devices_lock;

static list_t _devices;
static fs_node_t _dsp_fnode = {
	.name   = "dsp",
	.device = &_devices,
	.mask   = 0666,
	.flags  = FS_CHARDEVICE,
	.ioctl  = snd_dsp_ioctl,
	.write  = snd_dsp_write,
	.open   = snd_dsp_open,
	.close  = snd_dsp_close,
};
static fs_node_t _mixer_fnode = {
	.name  = "mixer",
	.mask  = 0666,
	.flags = FS_CHARDEVICE,
	.ioctl = snd_mixer_ioctl,
	.open  = snd_mixer_open,
	.close = snd_mixer_close,
};
static spin_lock_t _buffers_lock;
static list_t _buffers;
static uint32_t _next_device_id = SND_DEVICE_MAIN;

struct dsp_node {
	ring_buffer_t * rb;
	size_t samples;
	size_t written;
	int realtime;
};

int snd_register(snd_device_t * device) {
	int rv = 0;

	spin_lock(_devices_lock);
	device->id = _next_device_id;
	_next_device_id++;
	if (list_find(&_devices, device)) {
		rv = -1;
		goto snd_register_cleanup;
	}
	list_insert(&_devices, device);

snd_register_cleanup:
	spin_unlock(_devices_lock);
	return rv;
}

int snd_unregister(snd_device_t * device) {
	int rv = 0;

	node_t * node = list_find(&_devices, device);
	if (!node) {
		printf("attempted to unregister unknown audio sink: %s\n", device->name);
		goto snd_unregister_cleanup;
	}
	list_delete(&_devices, node);

snd_unregister_cleanup:
	spin_unlock(_devices_lock);
	return rv;
}

static ssize_t snd_dsp_write(fs_node_t * node, off_t offset, size_t size, uint8_t *buffer) {
	if (!_devices.length) return -1; /* No sink available. */

	struct dsp_node * dsp = node->device;

	size_t s = ring_buffer_available(dsp->rb);
	size_t out;
	if (size > s && dsp->realtime) {
		out = ring_buffer_write(dsp->rb, s & ~0x3, buffer);
	} else {
		out = ring_buffer_write(dsp->rb, size, buffer);
	}
	dsp->written += out / 4;

	return out;
}

static int snd_dsp_ioctl(fs_node_t * node, unsigned long request, void * argp) {
	/* Potentially use this to set sample rates in the future */
	struct dsp_node * dsp = node->device;
	if (request == 4) {
		dsp->realtime = 1;
	} else if (request == 5) {
		return dsp->samples;
	}
	return -1;
}

static void snd_dsp_open(fs_node_t * node, unsigned int flags) {
	/*
	 * XXX(gerow): A process could take the memory of the entire system by opening
	 * too many of these...
	 */
	/* Allocate a buffer for the node and keep a reference for ourselves */

	struct dsp_node * dsp = malloc(sizeof(struct dsp_node));
	dsp->rb = ring_buffer_create(SND_BUF_SIZE);
	dsp->samples = 0;
	dsp->written = 0;
	dsp->realtime = 0;
	node->device = dsp;
	spin_lock(_buffers_lock);
	list_insert(&_buffers, node->device);
	spin_unlock(_buffers_lock);
}

static void snd_dsp_close(fs_node_t * node) {
	struct dsp_node * dsp = node->device;
	spin_lock(_buffers_lock);
	list_delete(&_buffers, list_find(&_buffers, dsp));
	spin_unlock(_buffers_lock);

	ring_buffer_destroy(dsp->rb);
	free(dsp->rb);
	free(dsp);
}

static snd_device_t * snd_device_by_id(uint32_t device_id) {
	spin_lock(_devices_lock);
	snd_device_t * out = NULL;
	snd_device_t * cur = NULL;

	foreach(node, &_devices) {
		cur = node->value;
		if (cur->id == device_id) {
			out = cur;
		}
	}
	spin_unlock(_devices_lock);

	return out;
}

static int snd_mixer_ioctl(fs_node_t * node, unsigned long request, void * argp) {
	switch (request) {
		case SND_MIXER_GET_KNOBS: {
			snd_knob_list_t * list = argp;
			snd_device_t * device = snd_device_by_id(list->device);
			if (!device) {
				return -EINVAL;
			}
			list->num = device->num_knobs;
			for (uint32_t i = 0; i < device->num_knobs; i++) {
				list->ids[i] = device->knobs[i].id;
			}
			return 0;
		}
		case SND_MIXER_GET_KNOB_INFO: {
			snd_knob_info_t * info = argp;
			snd_device_t * device = snd_device_by_id(info->device);
			if (!device) {
				return -EINVAL;
			}
			for (uint32_t i = 0; i < device->num_knobs; i++) {
				if (device->knobs[i].id == info->id) {
					memcpy(info->name, device->knobs[i].name, sizeof(info->name));
					return 0;
				}
			}
			return -EINVAL;
		}
		case SND_MIXER_READ_KNOB: {
			snd_knob_value_t * value = argp;
			snd_device_t * device = snd_device_by_id(value->device);
			if (!device) {
				return -EINVAL;
			}
			return device->mixer_read(value->id, &value->val);
		}
		case SND_MIXER_WRITE_KNOB: {
			snd_knob_value_t * value = argp;
			snd_device_t * device = snd_device_by_id(value->device);
			if (!device) {
				return -EINVAL;
			}
			return device->mixer_write(value->id, value->val);
		}
		default: {
			return -EINVAL;
		}
	}
}

static void snd_mixer_open(fs_node_t * node, unsigned int flags) {
	return;
}

static void snd_mixer_close(fs_node_t * node) {
	return;
}

int snd_request_buf(snd_device_t * device, uint32_t size, uint8_t *buffer) {
	static int16_t tmp_buf[0x100];

	memset(buffer, 0, size);

	spin_lock(_buffers_lock);
	foreach(buf_node, &_buffers) {
		struct dsp_node * dsp = buf_node->value;
		ring_buffer_t * buf = dsp->rb;
		/* ~0x3 is to ensure we don't read partial samples or just a single channel */
		size_t bytes_left = MIN(ring_buffer_unread(buf) & ~0x3, size);
		int16_t * adding_ptr = (int16_t *) buffer;
		while (bytes_left) {
			size_t this_read_size = MIN(bytes_left, sizeof(tmp_buf));
			ring_buffer_read(buf, this_read_size, (uint8_t *)tmp_buf);
			dsp->samples += this_read_size / 4; /* 16 bits, 2 channels */
			/*
			 * Reduce the sample by a half so that multiple sources won't immediately
			 * cause awful clipping. This is kind of a hack since it would probably be
			 * better to just use some kind of compressor.
			 */
			for (size_t i = 0; i < N_ELEMENTS(tmp_buf); i++) {
				tmp_buf[i] /= 2;
			}
			for (size_t i = 0; i < this_read_size / sizeof(*adding_ptr); i++) {
				adding_ptr[i] += tmp_buf[i];
			}
			adding_ptr += this_read_size / sizeof(*adding_ptr);
			bytes_left -= this_read_size;
		}
	}
	spin_unlock(_buffers_lock);

	return size;
}

static snd_device_t * snd_main_device(void) {
	spin_lock(_devices_lock);
	foreach(node, &_devices) {
		spin_unlock(_devices_lock);
		return node->value;
	}

	spin_unlock(_devices_lock);
	return NULL;
}

void snd_install(void) {
	vfs_mount("/dev/dsp", &_dsp_fnode);
	vfs_mount("/dev/mixer", &_mixer_fnode);
}

