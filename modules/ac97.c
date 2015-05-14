/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Michael Gerow
 * Copyright (C) 2015 Kevin Lange
 *
 * Driver for the Intel AC'97.
 *
 * See <http://www.intel.com/design/chipsets/manuals/29802801.pdf>.
 */

#include <logging.h>
#include <mem.h>
#include <module.h>
#include <mod/shell.h>
#include <printf.h>
#include <pci.h>
#include <system.h>

/* Utility macros */
#define N_ELEMENTS(arr) (sizeof(arr) / sizeof((arr)[0]))

/* BARs! */
#define AC97_NAMBAR  0x10  /* Native Audio Mixer Base Address Register */
#define AC97_NABMBAR 0x14  /* Native Audio Bus Mastering Base Address Register */

/* Bus mastering IO port offsets */
#define AC97_PO_BDBAR 0x10  /* PCM out buffer descriptor BAR */
#define AC97_PO_CIV   0x14  /* PCM out current index value */
#define AC97_PO_LVI   0x15  /* PCM out last valid index */
#define AC97_PO_SR    0x16  /* PCM out status register */
#define AC97_PO_PICB  0x18  /* PCM out position in current buffer register */
#define AC97_PO_CR    0x1B  /* PCM out control register */

/* Bus mastering misc */
/* Buffer descriptor list constants */
#define AC97_BDL_LEN              32                    /* Buffer descriptor list length */
#define AC97_BDL_BUFFER_LEN       0x8000                /* Length of buffer in BDL */
#define AC97_CL_GET_LENGTH(cl)    ((cl) & 0xFFFF)       /* Decode length from cl */
#define AC97_CL_SET_LENGTH(cl, v) ((cl) = (v) & 0xFFFF) /* Encode length to cl */
#define AC97_CL_BUP               (1 << 30)             /* Buffer underrun policy in cl */
#define AC97_CL_IOC               (1 << 31)             /* Interrupt on completion flag in cl */

/* PCM out control register flags */
#define AC97_X_CR_RPBM  (1 << 0)  /* Run/pause bus master */
#define AC97_X_CR_RR    (1 << 1)  /* Reset registers */
#define AC97_X_CR_LVBIE (1 << 2)  /* Last valid buffer interrupt enable */
#define AC97_X_CR_FEIE  (1 << 3)  /* FIFO error interrupt enable */
#define AC97_X_CR_IOCE  (1 << 4)  /* Interrupt on completion enable */

/* Status register flags */
#define AC97_X_SR_DCH   (1 << 0)  /* DMA controller halted */
#define AC97_X_SR_CELV  (1 << 1)  /* Current equals last valid */
#define AC97_X_SR_LVBCI (1 << 2)  /* Last valid buffer completion interrupt */
#define AC97_X_SR_BCIS  (1 << 3)  /* Buffer completion interrupt status */
#define AC97_X_SR_FIFOE (1 << 3)  /* FIFO error */

/* Mixer IO port offsets */
#define AC97_RESET          0x00
#define AC97_MASTER_VOLUME  0x02
#define AC97_AUX_OUT_VOLUME 0x04
#define AC97_MONO_VOLUME    0x06
#define AC97_PCM_OUT_VOLUME 0x18

/* An entry in a buffer dscriptor list */
typedef struct {
	uint32_t pointer;  /* Pointer to buffer */
	uint32_t cl;       /* Control values and buffer length */
} __attribute__((packed)) ac97_bdl_entry_t;

typedef struct {
	uint32_t pci_device;
	uint16_t nabmbar;               /* Native audio bus mastring BAR */
	uint16_t nambar;                /* Native audio mixing BAR */
	size_t irq;                     /* This ac97's irq */
	uint8_t lvi;                    /* The currently set last valid index */
	ac97_bdl_entry_t * bdl;         /* Buffer descriptor list */
	uint16_t * bufs[AC97_BDL_LEN];  /* Virtual addresses for buffers in BDL */
	uint32_t bdl_p;
} ac97_device_t;

static ac97_device_t _device;

static void find_ac97(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {

	ac97_device_t * ac97 = extra;

	if ((vendorid == 0x8086) && (deviceid == 0x2415)) {
		ac97->pci_device = device;
	}

}

DEFINE_SHELL_FUNCTION(ac97_status, "[debug] AC'97 status values") {
	if (!_device.pci_device) {
		fprintf(tty, "No AC'97 device found.\n");
		return 1;
	}
	fprintf(tty, "AC'97 audio device is at 0x%x.\n", _device.pci_device);
	size_t irq = pci_read_field(_device.pci_device, PCI_INTERRUPT_LINE, 1);
	fprintf(tty, "IRQ: %d\n", irq);
	uint16_t command_register = pci_read_field(_device.pci_device, PCI_COMMAND, 2);
	fprintf(tty, "COMMAND: 0x%04x\n", command_register);
	fprintf(tty, "NABMBAR: 0x%04x\n", pci_read_field(_device.pci_device, AC97_NABMBAR, 2));
	fprintf(tty, "PO_BDBAR: 0x%08x\n", inportl(_device.nabmbar + AC97_PO_BDBAR));
	if (_device.bdl) {
		for (int i = 0; i < AC97_BDL_LEN; i++) {
			fprintf(tty, "bdl[%d].pointer: 0x%x\n", i, _device.bdl[i].pointer);
			fprintf(tty, "bdl[%d].cl: 0x%x\n", i, _device.bdl[i].cl);
		}
	}
	fprintf(tty, "PO_CIV: %d\n", inportb(_device.nabmbar + AC97_PO_CIV));
	fprintf(tty, "PO_PICB: 0x%04x\n", inportb(_device.nabmbar + AC97_PO_PICB));
	fprintf(tty, "PO_CR: 0x%02x\n", inportb(_device.nabmbar + AC97_PO_CR));
	fprintf(tty, "PO_LVI: 0x%02x\n", inportb(_device.nabmbar + AC97_PO_LVI));

	return 0;
}

static list_t * next_buffers_mutex = NULL;
static uint8_t sample_tmp[0x1000] = {1};
static size_t offset = 0;
static fs_node_t * file = NULL;
static int last_finish = 0;
static int last_playback_pid = 0;
static void do_write(size_t buffers) {
	for (size_t j = buffers; j < buffers + AC97_BDL_LEN / 2; j++) {
		for (size_t k = 0; k < AC97_BDL_BUFFER_LEN * sizeof(*_device.bufs[0]); k += 0x1000) {
			int read = read_fs(file, offset, 0x1000, sample_tmp);
			debug_print(INFO, "Writing buffer %d from offset 0x%x, read 0x%x", j, offset, read);
			for (size_t i = 0; i < 0x1000; i++) {
				((uint8_t *)_device.bufs[j])[i+k] = sample_tmp[i];
			}
			offset += 0x1000;
		}
	}
}
static void do_clear(size_t buffers) {
	for (size_t j = buffers; j < buffers + AC97_BDL_LEN / 2; j++) {
		memset((uint8_t *)_device.bufs[j], 0x00, AC97_BDL_BUFFER_LEN * sizeof(*_device.bufs[0]));
	}
}

static void playback_tasklet(void * data, char * name) {
	file = kopen((char*)data, 0);
	free(data);
	if (!file) task_exit(1);
	offset = 0;
	last_finish = 0;

	do_write(0);
	do_write(16);
	_device.lvi = AC97_BDL_LEN - 1;
	outportb(_device.nabmbar + AC97_PO_LVI, _device.lvi);
	outportb(_device.nabmbar + AC97_PO_CR, inportb(_device.nabmbar + AC97_PO_CR) | AC97_X_CR_RPBM);

	while (offset < file->length) {
		sleep_on(next_buffers_mutex);
		if (offset >= file->length) break;
		do_write(0);
		sleep_on(next_buffers_mutex);
		if (offset >= file->length) break;
		do_write(16);
	}

	debug_print(NOTICE, "Playback is done.");
	close_fs(file);
	file = NULL;

	last_finish = 3;

	task_exit(0);
}

DEFINE_SHELL_FUNCTION(ac97_play, "[debug] Play back a file") {
	if (file) {
		fprintf(tty, "Something is already playing, use ac97_stop first.\n");
		return 1;
	}
	char * fname = "/opt/examples/decorator_full.wav";
	if (argc > 1) {
		fname = argv[1];
	}

	fprintf(tty, "Playing %s\n", fname);
	last_playback_pid = create_kernel_tasklet(playback_tasklet, "[[ac97]]", strdup(fname));
	fprintf(tty, "Started playback thread, pid = %d\n", last_playback_pid);

	return 0;
}

static void stop_playback(void) {
	last_finish = 0;
	outportb(_device.nabmbar + AC97_PO_CR, inportb(_device.nabmbar + AC97_PO_CR) & ~AC97_X_CR_RPBM);
	outports(_device.nabmbar + AC97_PO_SR, AC97_X_SR_LVBCI);
}

DEFINE_SHELL_FUNCTION(ac97_stop, "[debug] Stop playback") {
	int f = 0;
	stop_playback();
	if (file) {
		offset = file->length;
		f = 1;
	}
	wakeup_queue(next_buffers_mutex);
	if (f) {
		int status;
		waitpid(last_playback_pid, &status, 0);
	}
	return 0;
}

static list_t * first_buffer_available = NULL;
static list_t * second_buffer_available = NULL;

static void irq_handler(struct regs * regs) {
	debug_print(NOTICE, "AC97 IRQ called");
	uint16_t sr = inports(_device.nabmbar + AC97_PO_SR);
	debug_print(NOTICE, "sr: 0x%04x", sr);
	if (sr & AC97_X_SR_LVBCI) {
		/* Stop playing */
		stop_playback();
		debug_print(NOTICE, "Last valid buffer completion interrupt handled");
	} else if (sr & AC97_X_SR_BCIS) {
		debug_print(NOTICE, "Buffer completion interrupt status start...");
		if (!last_finish || last_finish == 2) {
			do_clear(0);
			_device.lvi = AC97_BDL_LEN / 2 - 1;
			last_finish = 1;
			wakeup_queue(first_buffer_available);
		} else if (last_finish == 1) {
			do_clear(AC97_BDL_LEN / 2);
			_device.lvi = AC97_BDL_LEN - 1;
			last_finish = 2;
			wakeup_queue(second_buffer_available);
		} else if (last_finish == 3) {
			do_clear(0);
			do_clear(AC97_BDL_LEN / 2);
			stop_playback();
		}
		wakeup_queue(next_buffers_mutex);
		outportb(_device.nabmbar + AC97_PO_LVI, _device.lvi);
		outports(_device.nabmbar + AC97_PO_SR, AC97_X_SR_BCIS);
		debug_print(NOTICE, "Buffer completion interrupt status handled");
	} else if (sr & AC97_X_SR_FIFOE) {
		outports(_device.nabmbar + AC97_PO_SR, AC97_X_SR_FIFOE);
		debug_print(NOTICE, "FIFO error handled");
	}

	irq_ack(_device.irq);
}

static uint32_t cycle = 0;
static size_t get_buffer_for_offset(size_t offset) {
	return (offset / (AC97_BDL_BUFFER_LEN * sizeof(*_device.bufs[0]))) % AC97_BDL_LEN;
}
static size_t offset_in_buffer(size_t offset) {
	return offset % (AC97_BDL_BUFFER_LEN * sizeof(*_device.bufs[0]));
}
static int start_on_next = 0;
static uint32_t write_ac97(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	debug_print(NOTICE, "Writing 0x%x...", size);
	size_t written = 0;
	while (written < size) {
		size_t j = get_buffer_for_offset(cycle);
		size_t k = offset_in_buffer(cycle);

#if 1
		if (k == 0) {
			if (j == 0) {
				if (start_on_next == 1) {
				} else {
					if (start_on_next == 2) {
						outportb(_device.nabmbar + AC97_PO_CR, inportb(_device.nabmbar + AC97_PO_CR) | AC97_X_CR_RR);
						_device.lvi = AC97_BDL_LEN - 1;
						outportb(_device.nabmbar + AC97_PO_LVI, _device.lvi);
		outportl(_device.nabmbar + AC97_PO_BDBAR, _device.bdl_p);
						outportb(_device.nabmbar + AC97_PO_CR, inportb(_device.nabmbar + AC97_PO_CR) | AC97_X_CR_RPBM);
						start_on_next = 0;
					}
					sleep_on(first_buffer_available);
				}
			} else if (j == AC97_BDL_LEN / 2) {
				if (start_on_next == 1) {
					start_on_next = 2;
				} else {
					sleep_on(second_buffer_available);
				}
			}
		}
#endif

		debug_print(NOTICE, "Writing buffer %d starting at %d", j, k);
		size_t x = 0;
		while (written < size && (written == 0 || offset_in_buffer(cycle) != 0)) {
			((uint8_t *)_device.bufs[j])[k+x] = buffer[written];
			written++;
			cycle++;
			x++;
		}
	}


	return written;
}

static int ioctl_ac97(fs_node_t * node, int request, void * argp) {
	switch (request) {
		case 0xf00:
			return 0;
		case 0xf01:
			last_finish = 0;
			cycle = 0;
			start_on_next = 1;
			outportb(_device.nabmbar + AC97_PO_CR, inportb(_device.nabmbar + AC97_PO_CR) & ~AC97_X_CR_RPBM);
			do_clear(0);
			do_clear(16);
			return 0;
		case 0xf02:
			stop_playback();
			return 0;
		default:
			return -1;
	}
}


static fs_node_t * ac97_device_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	sprintf(fnode->name, "dsp");
	fnode->flags   = FS_CHARDEVICE;
	fnode->ioctl   = ioctl_ac97;
	fnode->write   = write_ac97;
	return fnode;
}


static int init(void) {
	debug_print(NOTICE, "Initializing AC97");
	pci_scan(&find_ac97, -1, &_device);
	if (!_device.pci_device) {
		return 1;
	}
	BIND_SHELL_FUNCTION(ac97_status);
	BIND_SHELL_FUNCTION(ac97_play);
	BIND_SHELL_FUNCTION(ac97_stop);
	_device.nabmbar = pci_read_field(_device.pci_device, AC97_NABMBAR, 2) & ((uint32_t) -1) << 1;
	_device.nambar = pci_read_field(_device.pci_device, PCI_BAR0, 4) & ((uint32_t) -1) << 1;
	_device.irq = pci_read_field(_device.pci_device, PCI_INTERRUPT_LINE, 1);
	if (!irq_is_handler_free(_device.irq)) {
		debug_print(ERROR, "AC97 IRQ conflict: IRQ %d already in use", _device.irq);
		return 1;
	}
	irq_install_handler(_device.irq, irq_handler);
	/* Enable all matter of interrupts */
	outportb(_device.nabmbar + AC97_PO_CR, AC97_X_CR_FEIE | AC97_X_CR_IOCE);

	/* Enable bus mastering and disable memory mapped space */
	pci_write_field(_device.pci_device, PCI_COMMAND, 2, 0x5);
	/* Put ourselves at a reasonable volume. */
	uint16_t volume = 0x03 | (0x03 << 8);
	outports(_device.nambar + AC97_MASTER_VOLUME, volume);
	outports(_device.nambar + AC97_PCM_OUT_VOLUME, volume);

	/* Allocate our BDL and our buffers */
	_device.bdl = (void *)kmalloc_p(AC97_BDL_LEN * sizeof(*_device.bdl), &_device.bdl_p);
	memset(_device.bdl, 0, AC97_BDL_LEN * sizeof(*_device.bdl));
	for (int i = 0; i < AC97_BDL_LEN; i++) {
		_device.bufs[i] = (uint16_t *)kmalloc_p(AC97_BDL_BUFFER_LEN * sizeof(*_device.bufs[0]),
												&_device.bdl[i].pointer);
		memset(_device.bufs[i], 0, AC97_BDL_BUFFER_LEN * sizeof(*_device.bufs[0]));
		AC97_CL_SET_LENGTH(_device.bdl[i].cl, AC97_BDL_BUFFER_LEN);
	}
	/* Set the midway buffer and the last buffer to interrupt */
	_device.bdl[AC97_BDL_LEN / 2].cl |= AC97_CL_IOC;
	_device.bdl[AC97_BDL_LEN - 1].cl |= AC97_CL_IOC;
	/* Tell the ac97 where our BDL is */
	outportl(_device.nabmbar + AC97_PO_BDBAR, _device.bdl_p);

	next_buffers_mutex = list_create();
	first_buffer_available = list_create();
	second_buffer_available = list_create();

	fs_node_t * dsp_node = ac97_device_create();
	vfs_mount("/dev/dsp", dsp_node);

	debug_print(NOTICE, "AC97 initialized successfully");

	return 0;
}

static int fini(void) {
	free(_device.bdl);
	for (int i = 0; i < AC97_BDL_LEN; i++) {
		free(_device.bufs[i]);
	}
	return 0;
}

MODULE_DEF(ac97, init, fini);
MODULE_DEPENDS(debugshell);
