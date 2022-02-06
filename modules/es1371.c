/**
 * @file kernel/audio/es1371.c
 * @brief Driver for the Ensoniq ES1371.
 * @package x86_64
 *
 * @ref http://www.vogons.org/download/file.php?id=13036&sid=30df81e15e2521deb842a79f451b1161
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */

#include <errno.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/pci.h>
#include <kernel/process.h>
#include <kernel/mmu.h>
#include <kernel/list.h>
#include <kernel/module.h>
#include <kernel/time.h>
#include <kernel/mod/snd.h>

#include <kernel/arch/x86_64/ports.h>
#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/irq.h>

#define ES_PORT_CONTROL      0x00
#define ES_PORT_STATUS       0x04
#define ES_PORT_UART_DATA    0x08
#define ES_PORT_UART_STS     0x09
#define ES_PORT_UART_TEST    0x0a
#define ES_PORT_MEMORY_PAGE  0x0c
#define ES_PORT_SRC_RW       0x10
#define ES_PORT_CODEC_RW     0x14
#define ES_PORT_LEGACY       0x18
#define ES_PORT_SERIAL       0x20
#define ES_PORT_P1_FRAME_CNT 0x24
#define ES_PORT_P2_FRAME_CNT 0x28
#define ES_PORT_R_FRAME_CNT  0x2c
#define ES_PORT_P1_BUF_ADDR  0x30
#define ES_PORT_P1_BUF_DEF   0x34
#define ES_PORT_P2_BUF_ADDR  0x38
#define ES_PORT_P2_BUF_DEF   0x3c

/**
 * Control bits
 */
#define ES_CTRL_SYNC_RES (1 << 14)
#define ES_CTRL_DAC2_EN  (1 << 5)

/**
 * Status bits
 */
#define ES_STATUS_INTR (1 << 31)
#define ES_STATUS_DAC2 (1 << 1)

/**
 * Serial control bits
 */
#define ES_SERIAL_P2_END_INC_MASK  (0x7 << 19)
#define ES_SERIAL_P2_END_INC_TWO   (2 << 19)
#define ES_SERIAL_P2_ST_INC_MASK   (0x7 << 16)
#define ES_SERIAL_P2_LOOP_MASK   (1 << 14)
#define ES_SERIAL_P2_PAUSE       (1 << 12)
#define ES_SERIAL_P2_INTR_EN     (1 << 9)
#define ES_SERIAL_P2_DAC_SEN     (1 << 6)
#define ES_SERIAL_P2_MODE_MASK   (0x3 << 2)
#define ES_SERIAL_P2_MODE_16BIT  (1 << 3)
#define ES_SERIAL_P2_MODE_STEREO (1 << 2)

/**
 * SRC RW register bits
 */
#define ES_SRC_REG_MASK (0xF << 19) /* SRC, DAC1, DAC2, ADC disable... */
#define ES_SRC_REG_WE   (1 << 24)
#define ES_SRC_REG_BUSY (1 << 23)
#define ES_SRC_REG(x)   ((x & 0x7F) << 25)

/**
 * 16-bit registers for the samplerate converter
 */
#define ES_SRC_P2_TRUNCN     0x74
#define ES_SRC_P2_INTREGS    0x75
#define ES_SRC_P2_ACCUMFRAC  0x76
#define ES_SRC_P2_VFREQFRAC  0x77

/**
 * Volume is specified with a sign bit, 3 integer bits, and the rest as fractional.
 * I am not sure what this actually gets used for - a multiplier?
 */
#define ES_SRC_P2_VOL_L      0x7E
#define ES_SRC_P2_VOL_R      0x7F

struct es1371_device {
	uint32_t pci_device;
	int portbase;
	int irq;
	int bits;
	int mask;
	uint32_t serial;
	int16_t * buf;
};

static struct es1371_device _device;

static snd_knob_t _knobs[] = {
	{
		"Master",
		SND_KNOB_MASTER
	},
};

static int es1371_mixer_read(uint32_t knob_id, uint32_t *val);
static int es1371_mixer_write(uint32_t knob_id, uint32_t val);

static snd_device_t _snd = {
	.name            = "Ensoniq ES1371",
	.device          = &_device,
	.playback_speed  = 48000,
	.playback_format = SND_FORMAT_L16SLE,

	.knobs     = _knobs,
	.num_knobs = 1,

	.mixer_read  = es1371_mixer_read,
	.mixer_write = es1371_mixer_write,
};

static void find_es1371(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	struct es1371_device * es1371 = extra;

	if ((vendorid == 0x1274) && (deviceid == 0x1371)) {
		es1371->pci_device = device;
	}

}

static int es1371_irq_handler(struct regs * regs) {
	uint32_t status = inportl(_device.portbase + ES_PORT_STATUS);
	if (!(status & ES_STATUS_INTR)) return 0;
	if (status & ES_STATUS_DAC2) {
		/* Reset the interrupt-waiting bit by toggling interrupts off and on for this DAC */
		outportl(_device.portbase + ES_PORT_SERIAL, _device.serial & ~ES_SERIAL_P2_INTR_EN);
		outportl(_device.portbase + ES_PORT_SERIAL, _device.serial);
		outportl(_device.portbase + ES_PORT_MEMORY_PAGE, 0x0c);
		uint32_t def = inportl(_device.portbase + ES_PORT_P2_BUF_DEF);
		snd_request_buf(&_snd, 0x1000, (uint8_t*)_device.buf + ((def & 0xFFFF0000) ? 0 : 0x1000));
	}
	irq_ack(_device.irq);
	return 1;
}

static int es1371_mixer_read(uint32_t knob_id, uint32_t *val) {
	switch (knob_id) {
		/* This is essentially the same as how we get the volume from the ICH AC'97, but
		 * we have to get the AC97 codec through this port access thing... */
		case SND_KNOB_MASTER: {
			outportl(_device.portbase + ES_PORT_CODEC_RW, (0x02 << 16) | (1 << 23));
			uint32_t tmp = inportl(_device.portbase + ES_PORT_CODEC_RW) & 0xFFFF;
			if (tmp == 0x8000) {
				*val = 0;
			} else {
				/* 6 bit value */
				*val = (tmp & _device.mask) << (sizeof(*val) * 8 - _device.bits);
				*val = ~*val;
				*val &= (uint32_t)_device.mask << (sizeof(*val) * 8 - _device.bits);
			}
			break;
		}
		default:
			return -1;
	}
	return 0;
}

static int es1371_mixer_write(uint32_t knob_id, uint32_t val) {
	switch (knob_id) {
		case SND_KNOB_MASTER: {
			uint16_t encoded;
			if (val == 0x0) {
				encoded = 0x8000;
			} else {
				val = ~val;
				val >>= (sizeof(val) * 8 - _device.bits);
				encoded = (val & 0xFF) | (val << 8);
			}
			outportl(_device.portbase + ES_PORT_CODEC_RW,
				(0x02 << 16) | encoded);
			break;
		}
		default:
			return -1;
	}
	return 0;
}

static void delay_yield(size_t subticks) {
	unsigned long s, ss;
	relative_time(0, subticks, &s, &ss);
	sleep_until((process_t *)this_core->current_process, s, ss);
	switch_task(0);
}

/**
 * @brief Write to the samplerate converter RAM
 */
static void src_write(int port, uint16_t value) {
	uint32_t x;
	while ((x = inportl(_device.portbase + ES_PORT_SRC_RW)) & ES_SRC_REG_BUSY);
	x &= ES_SRC_REG_MASK;
	x |= ES_SRC_REG_WE | ES_SRC_REG(port) | (value & 0xFFFF);
	outportl(_device.portbase + ES_PORT_SRC_RW, x);
}

static int es1371_install(int argc, char * argv[]) {
	pci_scan(&find_es1371, -1, &_device);
	if (!_device.pci_device) {
		return -ENODEV;
	}

	/* Get I/O port from PCI */
	_device.portbase = pci_read_field(_device.pci_device, PCI_BAR0, 4) & ((uint32_t)-1) << 1;

	/* Enable port-IO, bus mastering, interrupts */
	uint16_t command_reg = pci_read_field(_device.pci_device, PCI_COMMAND, 2);
	command_reg |= (1 << 2) | (1 << 0);
	command_reg &= ~(1 << 10);
	pci_write_field(_device.pci_device,
		PCI_COMMAND, 2, command_reg);

	/* Reset the controller */
	uint32_t ctrl   = 0;
	uint32_t serial = 0;
	outportl(_device.portbase + ES_PORT_CONTROL, ctrl);
	outportl(_device.portbase + ES_PORT_SERIAL,  serial);
	outportl(_device.portbase + ES_PORT_LEGACY,  0);
	outportl(_device.portbase + ES_PORT_CONTROL, ctrl | ES_CTRL_SYNC_RES);
	inportl(_device.portbase + ES_PORT_CONTROL);
	delay_yield(2000);
	outportl(_device.portbase + ES_PORT_CONTROL, ctrl);

	/* Get 8192 of audio buffer space */
	uintptr_t addr = mmu_allocate_n_frames(2) << 12;
	if (!addr) {
		return -ENODEV;
	}
	if (addr > 0xFFFFffff) {
		/* This thing only supports 32-bit physical addresses, so if we got something
		 * too high (unlikely at early boot) we need to bail. */
		printf("es1371: Allocated buffer is beyond the reach of 32-bit DMA engine.\n");
		return -ENODEV;
	}

	/* Map interrupt handler */
	_device.irq = pci_get_interrupt(_device.pci_device);
	irq_install_handler(_device.irq, es1371_irq_handler, "es1371");

	/* Zero out the audio buffer */
	_device.buf = mmu_map_from_physical(addr);
	memset(_device.buf, 0, 0x2000);

	/* Disable sound rate converter while we program it */
	outportl(_device.portbase + ES_PORT_SRC_RW,
		(1 << 22));

	/* Turn everything off? */
	for (int i = 0; i < 0x80; ++i) {
		src_write(i, 0);
	}

	/* Set for 48KHz */
	src_write(ES_SRC_P2_TRUNCN,  16 <<  4);
	src_write(ES_SRC_P2_INTREGS, 16 << 10);

	/* This seems to be the sample rate converter's left/right audio volume? */
	src_write(ES_SRC_P2_VOL_L, 0x1 << 12);
	src_write(ES_SRC_P2_VOL_R, 0x1 << 12);

	outportl(_device.portbase + ES_PORT_SRC_RW, 0);

	outportl(_device.portbase + ES_PORT_CODEC_RW, 0);
	delay_yield(2000);

	/* Set AC'97 codec volumes, which are inverted (-dB) */
	_device.bits = 5;
	_device.mask = 0x1f;
	outportl(_device.portbase + ES_PORT_CODEC_RW,
		(0x02 << 16)); /* Master */
	outportl(_device.portbase + ES_PORT_CODEC_RW,
		(0x18 << 16)); /* PCM OUT */

	outportb(ES_PORT_UART_STS, 0);
	outportb(ES_PORT_UART_TEST, 0);
	outportb(ES_PORT_STATUS, 0);

	/* Set up buffer for two pages we can swap between,
	 * which gives us good latency for immediate playback */
	outportl(_device.portbase + ES_PORT_MEMORY_PAGE, 0x0c);
	outportl(_device.portbase + ES_PORT_P2_BUF_ADDR, addr);
	outportl(_device.portbase + ES_PORT_P2_BUF_DEF, 0x7FF);
	outportl(_device.portbase + ES_PORT_P2_FRAME_CNT, 0x400);

	/* Configure playback mode */
	serial = inportl(_device.portbase + ES_PORT_SERIAL);
	serial &=
		~(ES_SERIAL_P2_LOOP_MASK | ES_SERIAL_P2_END_INC_MASK | ES_SERIAL_P2_DAC_SEN |
			ES_SERIAL_P2_PAUSE | ES_SERIAL_P2_ST_INC_MASK | ES_SERIAL_P2_MODE_MASK);
	serial |=
		ES_SERIAL_P2_INTR_EN |
		ES_SERIAL_P2_MODE_STEREO | ES_SERIAL_P2_MODE_16BIT | /* 16-bit sterio */
		ES_SERIAL_P2_END_INC_TWO;
	outportl(_device.portbase + ES_PORT_SERIAL, serial);
	_device.serial = serial;

	/* Turn it on! */
	ctrl = inportl(_device.portbase + ES_PORT_CONTROL);
	outportl(_device.portbase + ES_PORT_CONTROL, ES_CTRL_DAC2_EN | ctrl);

	snd_register(&_snd);
	return 0;
}

static int fini(void) {
	snd_unregister(&_snd);
	return 0;
}

struct Module metadata = {
	.name = "es1371",
	.init = es1371_install,
	.fini = fini,
};


