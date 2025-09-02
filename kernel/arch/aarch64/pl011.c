/**
 * @file  kernel/arch/aarch64/pl011.c
 * @brief Rudimentary serial driver for the pl011 uart
 *
 * TODO: Headers with sensible register names and macros for bits.
 * TODO: TTY configuration support.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2022-2025 K. Lange
 */
#include <stdint.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>
#include <kernel/pty.h>
#include <kernel/mmu.h>

#include <kernel/arch/aarch64/dtb.h>
#include <kernel/arch/aarch64/gic.h>

static int pl011_irq(process_t * this, int irq, void * data) {
	volatile uint32_t * uart_mapped = (volatile uint32_t *)data;
	uint32_t mis = uart_mapped[16];
	if (mis) {
		if (mis & (1 << 4)) {
			make_process_ready(this);
		}
		uart_mapped[17] = mis;
		return 1;
	}
	return 0;
}

static void pl011_fill_name(pty_t * pty, char * name) {
	snprintf(name, 100, "/dev/ttyS0");
}

static void pl011_write_out(pty_t * pty, uint8_t c) {
	volatile uint32_t * uart_mapped = (volatile uint32_t *)pty->_private;
	uart_mapped[0] = c;
}

static void pl011_thread(void * arg) {
	volatile uint32_t * uart_mapped = (volatile uint32_t *)arg;
	pty_t * pty = pty_new(NULL, 0);
	pty->write_out = pl011_write_out;
	pty->fill_name = pl011_fill_name;
	pty->slave->gid = 2; /* dialout group */
	pty->slave->mask = 0660;
	pty->_private = arg;
	vfs_mount("/dev/ttyS0", pty->slave);

	/* Set up interrupt callback */
	gic_assign_interrupt(1, pl011_irq, (void*)uart_mapped);

	/* Enable interrupts */
	uart_mapped[14] |= (1 << 4);
	asm volatile ("isb" ::: "memory");

	/* Handle incoming data */
	while (1) {
		while ((uart_mapped[6] & (1 << 4))) {
			switch_task(0);
		}

		uint8_t rx = uart_mapped[0];
		tty_input_process(pty, rx);
	}
}

void pl011_start(void) {
	uint32_t * uart = dtb_find_node_prefix("pl011");
	if (!uart) return;

	/* I know this is going to be 0x09000000, but let's find it anyway */
	uint32_t * reg = dtb_node_find_property(uart, "reg");
	uintptr_t  uart_base = swizzle(reg[3]);
	volatile uint32_t * uart_mapped = (volatile uint32_t*)mmu_map_mmio_region(uart_base, 0x1000);

	/*
	 * PL011 UART configuration
	 *
	 * Setup line control here so we do it atomically, otherwise doing it in the thread
	 * can cause other cores printing debug messages to write when the port is disabled,
	 * which causes QEMU to print warning messages.
	 *
	 * We are missing a few things because this is only intended to work with QEMU.
	 * We should probably add support for baud rate configuration and actually map
	 * the line control configuration to the TTY state.
	 */
	uart_mapped[12] = 0;     /* UARTCR  - Disable the UART */
	uart_mapped[11] = 0x70;  /* UARTLCR - Stick parity disabled, 8-bit words, tx+rx FIFO enabled; one stop bit, (odd parity), parity disabled, no break */
	uart_mapped[12] = 0x301; /* UARTCR  - Enable tx/rx, enable UART */

	spawn_worker_thread(pl011_thread, "[pl011]", (void*)uart_mapped);
}


