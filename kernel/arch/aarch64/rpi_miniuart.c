/**
 * @file  kernel/arch/aarch64/rpi_miniuart.c
 * @brief Rudimentary serial driver for the rpi's miniuart
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2022 K. Lange
 */
#include <stdint.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>
#include <kernel/pty.h>
#include <kernel/mmu.h>

#include <kernel/arch/aarch64/dtb.h>
#include <kernel/arch/aarch64/gic.h>

#define UART_BAUD 921600
//#define UART_BAUD 115200


static uintptr_t gpio_base = 0;

static uint32_t mmio_read(uintptr_t addr) {
	uint32_t res = *((volatile uint32_t*)(addr));
	return res;
}
static void mmio_write(uintptr_t addr, uint32_t val) {
	(*((volatile uint32_t*)(addr))) = val;
}

/**
 * GPIO initialization mostly from Adam Greenwood-Byrne's rpi4-osdev
 */
#define PERI_BASE 0xFE000000
#define GPIO_BASE (PERI_BASE + 0x200000)
#define GPFSEL0   (0x00)
#define GPSET0    (0x1c)
#define GPCLR0    (0x28)
#define GPPUPPDN0 (0xe4)

/**
 * AUX register offsets from the peripheral manual
 */
#define AUX_IRQ          0x00
#define AUX_ENABLES      0x04
#define AUX_MU_IO_REG    0x40
#define AUX_MU_IER_REG   0x44
#define AUX_MU_IIR_REG   0x48
#define AUX_MU_LCR_REG   0x4c
#define AUX_MU_MCR_REG   0x50
#define AUX_MU_LSR_REG   0x54
#define AUX_MU_CNTL_REG  0x60
#define AUX_MU_BAUD_REG  0x68

#define BAUD_CALC(rate) ((500000000UL/(rate*8))-1)

static int gpio_call(uint32_t pin, uint32_t value, uint32_t base, uint32_t field_size, uint32_t field_max) {
	uint32_t mask = (1 << field_size) - 1;
	if (pin > field_max) return 0;
	if (value > mask) return 0;

	uint32_t fields = 32 / field_size;
	uint32_t reg    = base + (pin / fields) * 4;
	uint32_t shift  = (pin % fields) * field_size;
	uint32_t cur    = mmio_read(gpio_base + reg);
	cur &= ~(mask << shift);
	cur |= (value << shift);
	mmio_write(gpio_base + reg, cur);

	return 1;
}

static int miniuart_irq(process_t * this, int irq, void * data) {
	uintptr_t uart_mapped = (uintptr_t)data;
	asm volatile ("dmb sy" ::: "memory");
	uint32_t aux_cause = mmio_read(uart_mapped + AUX_IRQ);
	if (aux_cause & 1) {
		uint32_t uart_iir =  mmio_read(uart_mapped + AUX_MU_IIR_REG);
		if (uart_iir & (1 << 2)) {
			make_process_ready(this);
		}
		return 1;
	}
	return 0;
}

static void miniuart_fill_name(pty_t * pty, char * name) {
	snprintf(name, 100, "/dev/ttyUART1");
}

static void miniuart_write_out(pty_t * pty, uint8_t c) {
	uintptr_t uart_mapped = (uintptr_t)pty->_private;
	while (!(mmio_read(uart_mapped + AUX_MU_LSR_REG) & 0x20));
	mmio_write(uart_mapped + AUX_MU_IO_REG, (uint8_t)c);
}

static void miniuart_thread(void * arg) {
	uintptr_t uart_mapped = (uintptr_t)arg;

	gic_assign_interrupt(0x5D, miniuart_irq, (void*)uart_mapped);

	mmio_write(uart_mapped + AUX_ENABLES,     1); /* Enable mini uart */
	mmio_write(uart_mapped + AUX_MU_IER_REG,  0); /* Disable interrupts while we set up */
	mmio_write(uart_mapped + AUX_MU_CNTL_REG, 0); /* Disable transmit/receive */
	mmio_write(uart_mapped + AUX_MU_LCR_REG,  3); /* 8-bit output (XXX shouldn't this just be '1'?) */
	mmio_write(uart_mapped + AUX_MU_MCR_REG,  0); /* RTS is high */
	mmio_write(uart_mapped + AUX_MU_IER_REG,  0); /* Disable interrupts again? */
	mmio_write(uart_mapped + AUX_MU_IIR_REG,  0xC6); /* ack and clear interrupts */
	mmio_write(uart_mapped + AUX_MU_BAUD_REG, BAUD_CALC(UART_BAUD));

	asm volatile ("dmb sy" ::: "memory");

	gpio_call(14, 0, GPPUPPDN0, 2, 53);
	gpio_call(14, 2, GPFSEL0, 3, 53);
	gpio_call(15, 0, GPPUPPDN0, 2, 53);
	gpio_call(15, 2, GPFSEL0, 3, 53);

	asm volatile ("dmb sy" ::: "memory");

	mmio_write(uart_mapped + AUX_MU_CNTL_REG, 3); /* tx, rx enable */

	pty_t * pty = pty_new(NULL, 0);
	pty->write_out = miniuart_write_out;
	pty->fill_name = miniuart_fill_name;
	pty->slave->gid = 2; /* dialout group */
	pty->slave->mask = 0660;
	pty->_private = arg;
	vfs_mount("/dev/ttyUART1", pty->slave);

	/* Enable interrupts */
	mmio_write(uart_mapped + AUX_MU_IER_REG, 1); /* enable receive interrupt */
	mmio_write(uart_mapped + AUX_MU_IIR_REG, 0xC6); /* ack and clear interrupts */
	asm volatile ("isb" ::: "memory");

	/* Handle incoming data */
	while (1) {
		while (!(mmio_read(uart_mapped + AUX_MU_LSR_REG) & 0x01)) {
			switch_task(0);
		}

		uint8_t rx = mmio_read(uart_mapped + AUX_MU_IO_REG);
		tty_input_process(pty, rx);
	}
}

void miniuart_start(void) {
	gpio_base = (uintptr_t)mmu_map_mmio_region(GPIO_BASE, 0x1000);
	void * uart_mapped = mmu_map_mmio_region(0xFE215000, 0x1000);

	spawn_worker_thread(miniuart_thread, "[miniuart]", uart_mapped);
}

