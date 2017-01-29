/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016 Kevin Lange
 */
#include <module.h>
#include <logging.h>
#include <printf.h>
#include <pci.h>
#include <mem.h>
#include <list.h>
#include <pipe.h>
#include <ipv4.h>
#include <mod/net.h>

static list_t * net_queue = NULL;
static spin_lock_t net_queue_lock = { 0 };
static list_t * rx_wait;

static uint32_t pcnet_device_pci = 0x00000000;
static uint32_t pcnet_io_base = 0;
static uint32_t pcnet_mem_base = 0;
static int pcnet_irq;
static uint8_t mac[6];

static uint32_t pcnet_buffer_phys;
static uint8_t *pcnet_buffer_virt;

static uint8_t * pcnet_rx_de_start;
static uint8_t * pcnet_tx_de_start;
static uint8_t * pcnet_rx_start;
static uint8_t * pcnet_tx_start;

static uint32_t pcnet_rx_de_phys;
static uint32_t pcnet_tx_de_phys;
static uint32_t pcnet_rx_phys;
static uint32_t pcnet_tx_phys;

static int pcnet_rx_buffer_id = 0;
static int pcnet_tx_buffer_id = 0;

#define PCNET_DE_SIZE 16
#define PCNET_BUFFER_SIZE 1548
#define PCNET_RX_COUNT 32
#define PCNET_TX_COUNT 8

static void find_pcnet(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	if ((vendorid == 0x1022) && (deviceid == 0x2000)) {
		*((uint32_t *)extra) = device;
	}
}

static void write_rap32(uint32_t value) {
	outportl(pcnet_io_base + 0x14, value);
}
static void write_rap16(uint16_t value) {
	outports(pcnet_io_base + 0x12, value);
}
static uint32_t read_csr32(uint32_t csr_no) {
	write_rap32(csr_no);
	return inportl(pcnet_io_base + 0x10);
}
static uint16_t read_csr16(uint16_t csr_no) {
	write_rap32(csr_no);
	return inports(pcnet_io_base + 0x10);
}
static void write_csr32(uint32_t csr_no, uint32_t value) {
	write_rap32(csr_no);
	outportl(pcnet_io_base + 0x10, value);
}
static void write_csr16(uint32_t csr_no, uint16_t value) {
	write_rap16(csr_no);
	outports(pcnet_io_base + 0x10, value);
}
static uint32_t read_bcr32(uint32_t bcr_no) {
	write_rap32(bcr_no);
	return inportl(pcnet_io_base + 0x1c);
}
static void write_bcr32(uint32_t bcr_no, uint32_t value) {
	write_rap32(bcr_no);
	outportl(pcnet_io_base + 0x1c, value);
}

static uint32_t virt_to_phys(uint8_t * virt) {
	return ((uintptr_t)virt - (uintptr_t)pcnet_buffer_virt) + pcnet_buffer_phys;
}

static int driver_owns(uint8_t * de_table, int index) {
	return (de_table[PCNET_DE_SIZE * index + 7] & 0x80) == 0;
}

static int next_tx_index(int current_tx_index) {
	int out = current_tx_index + 1;
	if (out == PCNET_TX_COUNT) {
		return 0;
	}
	return out;
}

static int next_rx_index(int current_rx_index) {
	int out = current_rx_index + 1;
	if (out == PCNET_RX_COUNT) {
		return 0;
	}
	return out;
}

static void init_descriptor(int index, int is_tx) {
	uint8_t * de_table = is_tx ? pcnet_tx_de_start : pcnet_rx_de_start;

	memset(&de_table[index * PCNET_DE_SIZE], 0, PCNET_DE_SIZE);

	uint32_t buf_addr = is_tx ? pcnet_tx_phys : pcnet_rx_phys;
	*(uint32_t *)&de_table[index * PCNET_DE_SIZE] = buf_addr + index * PCNET_BUFFER_SIZE;

	uint16_t bcnt = (uint16_t)(-PCNET_BUFFER_SIZE);
	bcnt &= 0x0FFF;
	bcnt |= 0xF000;
	*(uint16_t *)&de_table[index * PCNET_DE_SIZE + 4] = bcnt;

	if (!is_tx) {
		de_table[index * PCNET_DE_SIZE + 7] = 0x80;
	}
}

static void enqueue_packet(void * buffer) {
	spin_lock(net_queue_lock);
	list_insert(net_queue, buffer);
	spin_unlock(net_queue_lock);
}

static struct ethernet_packet * dequeue_packet(void) {
	while (!net_queue->length) {
		sleep_on(rx_wait);
	}

	spin_lock(net_queue_lock);
	node_t * n = list_dequeue(net_queue);
	void* value = n->value;
	free(n);
	spin_unlock(net_queue_lock);

	return value;
}

static uint8_t* pcnet_get_mac() {
	return mac;
}

static void pcnet_send_packet(uint8_t* payload, size_t payload_size) {
	if (!driver_owns(pcnet_tx_de_start, pcnet_tx_buffer_id)) {
		/* sleep? */
		debug_print(ERROR, "No transmit descriptors available. Bailing.");
		return;
	}
	if (payload_size > PCNET_BUFFER_SIZE) {
		debug_print(ERROR, "Packet too big; max is %d, got %d", PCNET_BUFFER_SIZE, payload_size);
		return;
	}
	memcpy((void *)(pcnet_tx_start + pcnet_tx_buffer_id * PCNET_BUFFER_SIZE), payload, payload_size);

	pcnet_tx_de_start[pcnet_tx_buffer_id * PCNET_DE_SIZE + 7] |= 0x3;

	uint16_t bcnt = (uint16_t)(-payload_size);
	bcnt &= 0x0FFF;
	bcnt |= 0xF000;
	*(uint16_t *)&pcnet_tx_de_start[pcnet_tx_buffer_id * PCNET_DE_SIZE + 4] = bcnt;

	pcnet_tx_de_start[pcnet_tx_buffer_id * PCNET_DE_SIZE + 7] |= 0x80;

	write_csr32(0, read_csr32(0) | (1 << 3));

	pcnet_tx_buffer_id = next_tx_index(pcnet_tx_buffer_id);
}

static int pcnet_irq_handler(struct regs *r) {

	write_csr32(0, read_csr32(0) | 0x0400);
	irq_ack(pcnet_irq);

	while (driver_owns(pcnet_rx_de_start, pcnet_rx_buffer_id)) {
		uint16_t plen = *(uint16_t *)&pcnet_rx_de_start[pcnet_rx_buffer_id * PCNET_DE_SIZE + 8];

		void * pbuf = (void *)(pcnet_rx_start + pcnet_rx_buffer_id * PCNET_BUFFER_SIZE);

		void * packet = malloc(plen);
		memcpy(packet, pbuf, plen);
		pcnet_rx_de_start[pcnet_rx_buffer_id * PCNET_DE_SIZE + 7] = 0x80;

		enqueue_packet(packet);

		pcnet_rx_buffer_id = next_rx_index(pcnet_rx_buffer_id);
	}
	wakeup_queue(rx_wait);

	return 1;
}

static void pcnet_init(void * data, char * name) {
	uint16_t command_reg = pci_read_field(pcnet_device_pci, PCI_COMMAND, 4) & 0xFFFF0000;
	if (command_reg & (1 << 2)) {
		debug_print(NOTICE, "Bus mastering already enabled.\n");
	}
	command_reg |= (1 << 2);
	command_reg |= (1 << 0);
	pci_write_field(pcnet_device_pci, PCI_COMMAND, 4, command_reg);

	pcnet_io_base  = pci_read_field(pcnet_device_pci, PCI_BAR0, 4) & 0xFFFFFFF0;
	pcnet_mem_base = pci_read_field(pcnet_device_pci, PCI_BAR1, 4) & 0xFFFFFFF0;

	pcnet_irq = pci_read_field(pcnet_device_pci, PCI_INTERRUPT_LINE, 1);
	irq_install_handler(pcnet_irq, pcnet_irq_handler);

	debug_print(NOTICE, "irq line: %d", pcnet_irq);
	debug_print(NOTICE, "io base: 0x%x", pcnet_io_base);

	/* Read MAC from EEPROM */
	mac[0] = inportb(pcnet_io_base + 0);
	mac[1] = inportb(pcnet_io_base + 1);
	mac[2] = inportb(pcnet_io_base + 2);
	mac[3] = inportb(pcnet_io_base + 3);
	mac[4] = inportb(pcnet_io_base + 4);
	mac[5] = inportb(pcnet_io_base + 5);

	/* Force reset */
	inportl(pcnet_io_base + 0x18);
	inports(pcnet_io_base + 0x14);

	unsigned long s, ss;
	relative_time(0, 10, &s, &ss);
	sleep_until((process_t *)current_process, s, ss);
	switch_task(0);

	debug_print(NOTICE, "pcnet return from sleep");

	/* set 32-bit mode */
	outportl(pcnet_io_base + 0x10, 0);

	/* SWSTYLE to 2 */
	uint32_t csr58 = read_csr32(58);
	csr58 &= 0xFFF0;
	csr58 |= 2;
	write_csr32(58, csr58);

	/* ASEL enable */
	uint32_t bcr2 = read_bcr32(2);
	bcr2 |= 0x2;
	write_bcr32(2, bcr2);

	debug_print(NOTICE, "device mac %2x:%2x:%2x:%2x:%2x:%2x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	if (!pcnet_buffer_virt) {
		debug_print(ERROR, "Failed.");
		return;
	}

	debug_print(WARNING, "phys: 0x%x, virt: 0x%x", pcnet_buffer_phys, pcnet_buffer_virt);

	pcnet_rx_de_start = pcnet_buffer_virt + 28;
	pcnet_tx_de_start = pcnet_rx_de_start + PCNET_RX_COUNT * PCNET_DE_SIZE;
	pcnet_rx_start    = pcnet_tx_de_start + PCNET_TX_COUNT * PCNET_DE_SIZE;
	pcnet_tx_start    = pcnet_rx_start + PCNET_RX_COUNT * PCNET_BUFFER_SIZE;

	pcnet_rx_de_phys  = virt_to_phys(pcnet_rx_de_start);
	pcnet_tx_de_phys  = virt_to_phys(pcnet_tx_de_start);
	pcnet_rx_phys     = virt_to_phys(pcnet_rx_start);
	pcnet_tx_phys     = virt_to_phys(pcnet_tx_start);

	/* set up descriptors */
	for (int i = 0; i < PCNET_RX_COUNT; i++) {
		init_descriptor(i, 0);
	}

	for (int i = 0; i < PCNET_TX_COUNT; i++) {
		init_descriptor(i, 1);
	}

	/* Set up device configuration structure */
	((uint16_t *)&pcnet_buffer_virt[0])[0] = 0x0000;
	pcnet_buffer_virt[2] = 5 << 4; /* RLEN << 4 */
	pcnet_buffer_virt[3] = 3 << 4; /* TLEN << 4 */
	pcnet_buffer_virt[4] = mac[0];
	pcnet_buffer_virt[5] = mac[1];
	pcnet_buffer_virt[6] = mac[2];
	pcnet_buffer_virt[7] = mac[3];
	pcnet_buffer_virt[8] = mac[4];
	pcnet_buffer_virt[9] = mac[5];

	pcnet_buffer_virt[10] = 0; /* reserved */
	pcnet_buffer_virt[11] = 0; /* reserved */

	pcnet_buffer_virt[12] = 0;
	pcnet_buffer_virt[13] = 0;
	pcnet_buffer_virt[14] = 0;
	pcnet_buffer_virt[15] = 0;
	pcnet_buffer_virt[16] = 0;
	pcnet_buffer_virt[17] = 0;
	pcnet_buffer_virt[18] = 0;
	pcnet_buffer_virt[19] = 0;

	((uint32_t *)&pcnet_buffer_virt[20])[0] = pcnet_rx_de_phys;
	((uint32_t *)&pcnet_buffer_virt[24])[0] = pcnet_tx_de_phys;

	/* Configure network */
	net_queue = list_create();
	rx_wait = list_create();

	write_csr32(1, 0xFFFF & pcnet_buffer_phys);
	write_csr32(2, 0xFFFF & (pcnet_buffer_phys >> 16));

	uint32_t a = read_csr32(1);
	uint32_t b = read_csr32(2);
	debug_print(ERROR, "csr1 = 0x%4x csr2= 0x%4x", a, b);

	uint16_t csr3 = read_csr32(3);
	if (csr3 & (1 << 10)) csr3 ^= (1 << 10);
	if (csr3 & (1 << 2)) csr3 ^= (1 << 2);
	csr3 |= (1 << 9);
	csr3 |= (1 << 8);
	write_csr32(3, csr3); /* Disable interrupt on init */
	write_csr32(4, read_csr32(4) | (1 << 1) | (1 << 12) | (1 << 14)); /* pad */

	write_csr32(0, read_csr32(0) | (1 << 0) | (1 << 6)); /* do it */

	while ((read_csr32(0) & (1 << 8)) == 0) {
		/* herp */
	}

	/* Start card */
	uint16_t csr0 = read_csr32(0);
	if (csr0 & (1 << 0)) csr0 ^= (1 << 0);
	if (csr0 & (1 << 2)) csr0 ^= (1 << 2);
	csr0 |= (1 << 1);
	write_csr32(0, csr0);

	debug_print(NOTICE, "Card start.");

	init_netif_funcs(pcnet_get_mac, dequeue_packet, pcnet_send_packet, "AMD PCnet FAST II/III");

}

static int init(void) {
	pci_scan(&find_pcnet, -1, &pcnet_device_pci);

	if (!pcnet_device_pci) {
		debug_print(WARNING, "No PCNET device found.");
		return 1;
	}

	/* Initialize ring buffers */
	debug_print(WARNING, "Request a large continuous chunk of memory.");
	/* This fits 32x1548 (rx) + 8x1548 (tx) + 32x16 (rx DE) + 8x16 (tx DE) */
	pcnet_buffer_virt = (void*)kvmalloc_p(0x10000, &pcnet_buffer_phys);

	create_kernel_tasklet(pcnet_init, "[pcnet]", NULL);

	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(pcnet, init, fini);
MODULE_DEPENDS(net);
