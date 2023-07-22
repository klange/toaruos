/**
 * @file  kernel/arch/aarch64/virtio.c
 * @brief Rudimentary, hacky implementations of virtio input devices.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2022 K. Lange
 */
#include <stdint.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>
#include <kernel/pci.h>
#include <kernel/pipe.h>
#include <kernel/video.h>
#include <kernel/mouse.h>
#include <kernel/time.h>

#include <kernel/arch/aarch64/gic.h>

static fs_node_t * mouse_pipe;
static fs_node_t * vmmouse_pipe;
static fs_node_t * keyboard_pipe;

struct virtio_device_cfg {
	volatile uint8_t select;
	volatile uint8_t subsel;
	volatile uint8_t size;
	volatile uint8_t pad[5];
	union {
		struct {
			volatile uint32_t min;
			volatile uint32_t max;
			volatile uint32_t fuzz;
			volatile uint32_t flat;
			volatile uint32_t res;
		} tablet_data;
		uint8_t str[128];
	} data;
};

struct virtio_common_cfg {
	volatile uint32_t dev_feature_select;
	volatile uint32_t dev_feature;
	volatile uint32_t guest_feature_select;
	volatile uint32_t guest_feature;
	volatile uint16_t msix;
	volatile uint16_t queues;
	volatile uint8_t  device_status;
	volatile uint8_t  config_generation;

	volatile uint16_t queue_select;
	volatile uint16_t queue_size;
	volatile uint16_t queue_msix_vector;
	volatile uint16_t queue_enable;
	volatile uint16_t queue_notify_off;
	/* queue stuff */

	volatile uint64_t queue_desc;
	volatile uint64_t queue_avail;
	volatile uint64_t queue_used;
};

struct virtio_buffer {
	uint64_t  addr;
	uint32_t  length;
	uint16_t  flags;
	uint16_t  next;
};

struct virtio_avail {
	uint16_t flags;
	volatile uint16_t index;
	uint16_t ring[64];
	uint16_t int_index;
};

struct virtio_ring {
	uint32_t index;
	uint32_t length;
};

struct virtio_used {
	uint16_t flags;
	volatile uint16_t index;
	struct virtio_ring ring[64];
	uint16_t int_index;
};

struct virtio_queue {
	struct virtio_buffer buffers[64];
	struct virtio_avail  available;
	struct virtio_used   used;
};

struct virtio_input_event {
	uint16_t type;
	uint16_t code;
	uint32_t value;
};

int virtio_tablet_responder(process_t * this, int irq, void * data) {
	uint8_t cause = *(volatile uint8_t *)data;
	if (cause == 1) {
		make_process_ready(this);
		return 1;
	}
	return 0;
}

int virtio_keyboard_responder(process_t * this, int irq, void * data) {
	uint8_t cause = *(volatile uint8_t *)data;
	if (cause == 1) {
		make_process_ready(this);
		return 1;
	}
	return 0;
}

static void try_to_get_boot_processor(void) {
	/* We would prefer these virtio startup processes run on the boot CPU, but
	 * it's not the end of the world if they don't. If we're not on the boot
	 * CPU, yield for a bit to try to get on it... */
	uint64_t expire = arch_perf_timer() + 100000UL * arch_cpu_mhz();
	while (this_core->cpu_id != 0) {
		if (arch_perf_timer() >= expire) break;
		switch_task(1);
	}
}

static void virtio_tablet_thread(void * data) {
	try_to_get_boot_processor();

	uint32_t device = (uintptr_t)data;
	uintptr_t t = 0x12000000;
	pci_write_field(device, PCI_BAR4, 4, t|8);
	pci_write_field(device, PCI_COMMAND, 2, 4|2|1);

	struct virtio_device_cfg * cfg = (void*)((char*)mmu_map_mmio_region(t + 0x2000, 0x1000));
	cfg->select = 1; /* ask for name */
	cfg->subsel = 0;
	asm volatile ("isb" ::: "memory");
	dprintf("virtio: found '%s'\n", cfg->data.str);

	void * irq_region = mmu_map_mmio_region(t + 0x1000, 0x1000);
	int irq;
	gic_map_pci_interrupt("virtio-tablet", device, &irq, virtio_tablet_responder, irq_region);
	dprintf("virtio-tablet: irq is %d\n", irq);

	/* figure out range values */
	cfg->select = 0x12;
	cfg->subsel = 0; /* X */
	asm volatile ("isb" ::: "memory");
	uint32_t max_x = cfg->data.tablet_data.max;
	cfg->select = 0x12;
	cfg->subsel = 1; /* X */
	asm volatile ("isb" ::: "memory");
	uint32_t max_y = cfg->data.tablet_data.max;

	dprintf("virtio: %d x %d max coordinates\n",
		max_x, max_y);

	cfg->select = 0;
	cfg->subsel = 0;
	asm volatile ("isb" ::: "memory");

	struct virtio_common_cfg * common = (void*)((char*)mmu_map_mmio_region(t, 0x1000));

	common->device_status = 0;
	asm volatile ("isb" ::: "memory");

	int queue_size = common->queue_size;
	dprintf("virtio: queue size is %u\n",
		queue_size);

	/* get us one page */
	size_t queue_phys = mmu_allocate_a_frame() << 12;
	struct virtio_queue * queue = mmu_map_mmio_region(queue_phys, 4096);
	asm volatile ("isb" ::: "memory");
	memset(queue, 0, sizeof(struct virtio_queue));
	asm volatile ("isb" ::: "memory");

	common->queue_select = 0;
	common->queue_desc = queue_phys;
	common->queue_avail = (queue_phys) + offsetof(struct virtio_queue, available);
	common->queue_used = (queue_phys) + offsetof(struct virtio_queue, used);
	asm volatile ("isb" ::: "memory");

	size_t buffers_base = mmu_allocate_a_frame() << 12;
	volatile struct virtio_input_event * buffers = mmu_map_mmio_region(buffers_base, 4096);
	mmu_get_page((uintptr_t)buffers, 0)->bits.attrindx = 2;

	for (int i = 0; i < queue_size; ++i) {
		queue->buffers[i].addr = buffers_base + i * sizeof(struct virtio_input_event);
		queue->buffers[i].length = sizeof(struct virtio_input_event);
		queue->buffers[i].flags = 2;
		queue->buffers[i].next = 0;
		queue->available.ring[i] = i;
	}

	queue->available.index = 0;
	asm volatile ("isb" ::: "memory");
	common->queue_enable = 1;
	asm volatile ("isb" ::: "memory");
	common->device_status = 4;
	asm volatile ("isb" ::: "memory");

	uint16_t index = 0;

	uint32_t x = 0;
	uint32_t y = 0;
	int button_left = 0;
	int button_right = 0;
	int button_middle = 0;
	int button_scroll_down = 0;
	int button_scroll_up = 0;

	queue->available.index = queue_size-1;

	while (1) {
		/* Inform the device we have room */
		while (queue->used.index == index) {
			switch_task(0);
			asm volatile ("dc ivac, %0\ndsb sy" :: "r"(&queue->used) : "memory");
		}

		uint16_t them = queue->used.index;

		for (; index != them; index++) {
			asm volatile ("dc ivac, %0\ndsb sy" :: "r"(&buffers[index%queue_size]) : "memory");
			struct virtio_input_event evt = buffers[index%queue_size];
			while (evt.type == 0xFF) {
				evt = buffers[index%queue_size];
				dprintf("virtio-tablet: bad packet %d (them=%d)\n", index, them);
			}
			buffers[index%queue_size].type = 0xFF;
			asm volatile ("isb\ndsb sy" :: "r"(buffers) : "memory");
			if (evt.type == 3) {
				/* movement */
				if (evt.code == 0) {
					x = (evt.value * lfb_resolution_x) / max_x;
				} else if (evt.code == 1) {
					y = (evt.value * lfb_resolution_y) / max_y;
				}
			} else if (evt.type == 1) {
				/* button */
				if (evt.code == 0x110) {
					button_left = evt.value;
				} else if (evt.code == 0x111) {
					button_right = evt.value;
				} else if (evt.code == 0x112) {
					button_middle = evt.value;
				} else if (evt.code == 0x150) {
					button_scroll_down = 1;
				} else if (evt.code == 0x151) {
					button_scroll_up = 1;
				}

			} else if (evt.type == 0) {
#define DISCARD_POINT 32
				mouse_device_packet_t packet;
				packet.magic = MOUSE_MAGIC;
				packet.x_difference = x;
				packet.y_difference = y;
				packet.buttons =
					(button_left ? LEFT_CLICK : 0) |
					(button_right ? RIGHT_CLICK : 0) |
					(button_middle ? MIDDLE_CLICK : 0) |
					(button_scroll_down ? MOUSE_SCROLL_DOWN : 0) |
					(button_scroll_up ? MOUSE_SCROLL_UP : 0);

				button_scroll_down = 0;
				button_scroll_up = 0;

				mouse_device_packet_t bitbucket;
				while (pipe_size(vmmouse_pipe) > (int)(DISCARD_POINT * sizeof(packet))) {
					read_fs(vmmouse_pipe, 0, sizeof(packet), (uint8_t *)&bitbucket);
				}
				write_fs(vmmouse_pipe, 0, sizeof(packet), (uint8_t *)&packet);
			}
			//buffers[index%queue_size].type = 0xFF;
			asm volatile ("isb" ::: "memory");
			queue->available.index++;
		}
	}
}

static const uint8_t ext_key_map[256] = {
	[0x63] = 0x37, /* print screen */
	[0x66] = 0x47, /* home */
	[0x67] = 0x48, /* UP */
	[0x68] = 0x49, /* page up */
	[0x6c] = 0x50, /* DOWN */
	[0x69] = 0x4B, /* LEFT */
	[0x6a] = 0x4D, /* RIGHT */
	[0x6b] = 0x4F, /* end */
	[0x6d] = 0x51, /* page down */
	[0x7d] = 0x5b, /* left super */
};

static void virtio_keyboard_thread(void * data) {
	try_to_get_boot_processor();

	uint32_t device = (uintptr_t)data;
	uintptr_t t = 0x12100000;
	pci_write_field(device, PCI_BAR4, 4, t|8);
	pci_write_field(device, PCI_COMMAND, 2, 4|2|1);
	struct virtio_device_cfg * cfg = (void*)((char*)mmu_map_mmio_region(t + 0x2000, 0x1000));
	cfg->select = 1; /* ask for name */
	cfg->subsel = 0;
	asm volatile ("isb" ::: "memory");
	dprintf("virtio: found '%s'\n", cfg->data.str);

	void * irq_region = mmu_map_mmio_region(t + 0x1000, 0x1000);
	int irq;
	gic_map_pci_interrupt("virtio-keyboard", device, &irq, virtio_keyboard_responder, irq_region);
	dprintf("virtio-keyboard: irq is %d\n", irq);

	cfg->select = 0;
	cfg->subsel = 0;
	asm volatile ("isb" ::: "memory");

	struct virtio_common_cfg * common = (void*)((char*)mmu_map_mmio_region(t, 0x1000));

	common->device_status = 0;
	asm volatile ("isb" ::: "memory");

	int queue_size = common->queue_size;
	dprintf("virtio: queue size is %u\n",
		queue_size);

	/* get us one page */
	size_t queue_phys = mmu_allocate_a_frame() << 12;
	struct virtio_queue * queue = mmu_map_mmio_region(queue_phys, 4096);
	asm volatile ("isb" ::: "memory");
	memset(queue, 0, sizeof(struct virtio_queue));
	asm volatile ("isb" ::: "memory");

	common->queue_select = 0;
	common->queue_desc = queue_phys;
	common->queue_avail = (queue_phys) + offsetof(struct virtio_queue, available);
	common->queue_used = (queue_phys) + offsetof(struct virtio_queue, used);
	asm volatile ("isb" ::: "memory");

	size_t buffers_base = mmu_allocate_a_frame() << 12;
	volatile struct virtio_input_event * buffers = mmu_map_mmio_region(buffers_base, 4096);
	mmu_get_page((uintptr_t)buffers, 0)->bits.attrindx = 2;

	for (int i = 0; i < queue_size; ++i) {
		queue->buffers[i].addr = buffers_base + i * sizeof(struct virtio_input_event);
		queue->buffers[i].length = sizeof(struct virtio_input_event);
		queue->buffers[i].flags = 2;
		queue->buffers[i].next = 0;
		queue->available.ring[i] = i;
	}

	queue->available.index = 0;
	asm volatile ("isb" ::: "memory");
	common->queue_enable = 1;
	asm volatile ("isb" ::: "memory");
	common->device_status = 4;
	asm volatile ("isb" ::: "memory");

	uint16_t index = 0;

	queue->available.index = queue_size-1;

	while (1) {
		/* Inform the device we have room */
		while (queue->used.index == index) {
			switch_task(0);
			asm volatile ("dc ivac, %0\ndsb sy" :: "r"(&queue->used) : "memory");
		}

		uint16_t them = queue->used.index;

		for (; index != them; index++) {
			asm volatile ("dc ivac, %0\ndsb sy" :: "r"(&buffers[index%queue_size]) : "memory");
			struct virtio_input_event evt = buffers[index%queue_size];
			while (evt.type == 0xFF) {
				evt = buffers[index%queue_size];
				dprintf("virtio-tablet: bad packet %d (them=%d)\n", index, them);
			}
			buffers[index%queue_size].type = 0xFF;
			asm volatile ("isb\ndsb sy" :: "r"(buffers) : "memory");
			if (evt.type == 1) {
				/* need to back-convert which is a pain in the ass */
				if (evt.code < 0x49) {
					uint8_t scancode = evt.code;
					if (evt.value == 0) {
						scancode |= 0x80;
					}
					uint8_t bitbucket;
					while (pipe_size(keyboard_pipe) > (int)(DISCARD_POINT)) {
						read_fs(keyboard_pipe, 0, 1, (uint8_t *)&bitbucket);
					}
					write_fs(keyboard_pipe, 0, 1, (uint8_t *)&scancode);
				} else if (ext_key_map[evt.code]) {
					uint8_t data[] = {0xE0, 0};
					data[1] = ext_key_map[evt.code] | ((evt.value == 0) ? 0x80 : 0);
					uint8_t bitbucket;
					while (pipe_size(keyboard_pipe) > (int)(DISCARD_POINT)) {
						read_fs(keyboard_pipe, 0, 1, (uint8_t *)&bitbucket);
					}
					write_fs(keyboard_pipe, 0, 2, (uint8_t *)data);
				} else {
					dprintf("virtio: unmapped keycode %d\n", evt.code);
				}
			}
			asm volatile ("isb" ::: "memory");
			queue->available.index++;
		}
	}
}

static void virtio_input_maybe(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if (v == 0x1af4 && d == 0x1052) {
		if (pci_find_type(device) == 0x0900) {
			spawn_worker_thread(virtio_keyboard_thread, "[virtio-keyboard]", (void*)(uintptr_t)device);
		} else if (pci_find_type(device) == 0x0980) {
			spawn_worker_thread(virtio_tablet_thread, "[virtio-tablet]", (void*)(uintptr_t)device);
		}
	}

}

void null_input(void) {
	mouse_pipe = make_pipe(128);
	mouse_pipe->flags = FS_CHARDEVICE;
	vfs_mount("/dev/mouse", mouse_pipe);

	vmmouse_pipe = make_pipe(4096);
	vmmouse_pipe->flags = FS_CHARDEVICE;
	vfs_mount("/dev/vmmouse", vmmouse_pipe);

	keyboard_pipe = make_pipe(128);
	keyboard_pipe->flags = FS_CHARDEVICE;
	vfs_mount("/dev/kbd", keyboard_pipe);

}

void virtio_input(void) {
	null_input(); /* setup pipes */
	pci_scan(virtio_input_maybe, -1, NULL);
}


