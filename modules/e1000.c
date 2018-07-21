/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2017-2018 K. Lange
 */
#include <kernel/module.h>
#include <kernel/logging.h>
#include <kernel/printf.h>
#include <kernel/pci.h>
#include <kernel/mem.h>
#include <kernel/pipe.h>
#include <kernel/ipv4.h>
#include <kernel/mod/net.h>

#include <toaru/list.h>

#define E1000_LOG_LEVEL NOTICE

static uint32_t e1000_device_pci = 0x00000000;
static int e1000_irq = 0;
static uintptr_t mem_base = 0;
static int has_eeprom = 0;
static uint8_t mac[6];
static int rx_index = 0;
static int tx_index = 0;

static list_t * net_queue = NULL;
static spin_lock_t net_queue_lock = { 0 };
static list_t * rx_wait;

static uint32_t mmio_read32(uintptr_t addr) {
	return *((volatile uint32_t*)(addr));
}
static void mmio_write32(uintptr_t addr, uint32_t val) {
	(*((volatile uint32_t*)(addr))) = val;
}

static void write_command(uint16_t addr, uint32_t val) {
	mmio_write32(mem_base + addr, val);
}

static uint32_t read_command(uint16_t addr) {
	return mmio_read32(mem_base + addr);
}

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

struct rx_desc {
	volatile uint64_t addr;
	volatile uint16_t length;
	volatile uint16_t checksum;
	volatile uint8_t  status;
	volatile uint8_t  errors;
	volatile uint16_t special;
} __attribute__((packed)); /* this looks like it should pack fine as-is */

struct tx_desc {
	volatile uint64_t addr;
	volatile uint16_t length;
	volatile uint8_t  cso;
	volatile uint8_t  cmd;
	volatile uint8_t  status;
	volatile uint8_t  css;
	volatile uint16_t special;
} __attribute__((packed));

static uint8_t * rx_virt[E1000_NUM_RX_DESC];
static uint8_t * tx_virt[E1000_NUM_TX_DESC];
static struct rx_desc * rx;
static struct tx_desc * tx;
static uintptr_t rx_phys;
static uintptr_t tx_phys;

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

static uint8_t* get_mac() {
	return mac;
}

#define E1000_REG_CTRL       0x0000
#define E1000_REG_STATUS     0x0008
#define E1000_REG_EEPROM     0x0014
#define E1000_REG_CTRL_EXT   0x0018

#define E1000_REG_RCTRL      0x0100
#define E1000_REG_RXDESCLO   0x2800
#define E1000_REG_RXDESCHI   0x2804
#define E1000_REG_RXDESCLEN  0x2808
#define E1000_REG_RXDESCHEAD 0x2810
#define E1000_REG_RXDESCTAIL 0x2818

#define E1000_REG_TCTRL      0x0400
#define E1000_REG_TXDESCLO   0x3800
#define E1000_REG_TXDESCHI   0x3804
#define E1000_REG_TXDESCLEN  0x3808
#define E1000_REG_TXDESCHEAD 0x3810
#define E1000_REG_TXDESCTAIL 0x3818

#define RCTL_EN                         (1 << 1)    /* Receiver Enable */
#define RCTL_SBP                        (1 << 2)    /* Store Bad Packets */
#define RCTL_UPE                        (1 << 3)    /* Unicast Promiscuous Enabled */
#define RCTL_MPE                        (1 << 4)    /* Multicast Promiscuous Enabled */
#define RCTL_LPE                        (1 << 5)    /* Long Packet Reception Enable */
#define RCTL_LBM_NONE                   (0 << 6)    /* No Loopback */
#define RCTL_LBM_PHY                    (3 << 6)    /* PHY or external SerDesc loopback */
#define RTCL_RDMTS_HALF                 (0 << 8)    /* Free Buffer Threshold is 1/2 of RDLEN */
#define RTCL_RDMTS_QUARTER              (1 << 8)    /* Free Buffer Threshold is 1/4 of RDLEN */
#define RTCL_RDMTS_EIGHTH               (2 << 8)    /* Free Buffer Threshold is 1/8 of RDLEN */
#define RCTL_MO_36                      (0 << 12)   /* Multicast Offset - bits 47:36 */
#define RCTL_MO_35                      (1 << 12)   /* Multicast Offset - bits 46:35 */
#define RCTL_MO_34                      (2 << 12)   /* Multicast Offset - bits 45:34 */
#define RCTL_MO_32                      (3 << 12)   /* Multicast Offset - bits 43:32 */
#define RCTL_BAM                        (1 << 15)   /* Broadcast Accept Mode */
#define RCTL_VFE                        (1 << 18)   /* VLAN Filter Enable */
#define RCTL_CFIEN                      (1 << 19)   /* Canonical Form Indicator Enable */
#define RCTL_CFI                        (1 << 20)   /* Canonical Form Indicator Bit Value */
#define RCTL_DPF                        (1 << 22)   /* Discard Pause Frames */
#define RCTL_PMCF                       (1 << 23)   /* Pass MAC Control Frames */
#define RCTL_SECRC                      (1 << 26)   /* Strip Ethernet CRC */

#define RCTL_BSIZE_256                  (3 << 16)
#define RCTL_BSIZE_512                  (2 << 16)
#define RCTL_BSIZE_1024                 (1 << 16)
#define RCTL_BSIZE_2048                 (0 << 16)
#define RCTL_BSIZE_4096                 ((3 << 16) | (1 << 25))
#define RCTL_BSIZE_8192                 ((2 << 16) | (1 << 25))
#define RCTL_BSIZE_16384                ((1 << 16) | (1 << 25))

#define TCTL_EN                         (1 << 1)    /* Transmit Enable */
#define TCTL_PSP                        (1 << 3)    /* Pad Short Packets */
#define TCTL_CT_SHIFT                   4           /* Collision Threshold */
#define TCTL_COLD_SHIFT                 12          /* Collision Distance */
#define TCTL_SWXOFF                     (1 << 22)   /* Software XOFF Transmission */
#define TCTL_RTLC                       (1 << 24)   /* Re-transmit on Late Collision */

#define CMD_EOP                         (1 << 0)    /* End of Packet */
#define CMD_IFCS                        (1 << 1)    /* Insert FCS */
#define CMD_IC                          (1 << 2)    /* Insert Checksum */
#define CMD_RS                          (1 << 3)    /* Report Status */
#define CMD_RPS                         (1 << 4)    /* Report Packet Sent */
#define CMD_VLE                         (1 << 6)    /* VLAN Packet Enable */
#define CMD_IDE                         (1 << 7)    /* Interrupt Delay Enable */

static int eeprom_detect(void) {

	write_command(E1000_REG_EEPROM, 1);

	for (int i = 0; i < 100000 && !has_eeprom; ++i) {
		uint32_t val = read_command(E1000_REG_EEPROM);
		if (val & 0x10) has_eeprom = 1;
	}

	return 0;
}

static uint16_t eeprom_read(uint8_t addr) {
	uint32_t temp = 0;
	write_command(E1000_REG_EEPROM, 1 | ((uint32_t)(addr) << 8));
	while (!((temp = read_command(E1000_REG_EEPROM)) & (1 << 4)));
	return (uint16_t)((temp >> 16) & 0xFFFF);
}


static void find_e1000(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	if ((vendorid == 0x8086) && (deviceid == 0x100e || deviceid == 0x1004 || deviceid == 0x100f)) {
		*((uint32_t *)extra) = device;
	}
}

static void read_mac(void) {
	if (has_eeprom) {
		uint32_t t;
		t = eeprom_read(0);
		mac[0] = t & 0xFF;
		mac[1] = t >> 8;
		t = eeprom_read(1);
		mac[2] = t & 0xFF;
		mac[3] = t >> 8;
		t = eeprom_read(2);
		mac[4] = t & 0xFF;
		mac[5] = t >> 8;
	} else {
		uint8_t * mac_addr = (uint8_t *)(mem_base + 0x5400);
		for (int i = 0; i < 6; ++i) {
			mac[i] = mac_addr[i];
		}
	}
}

static int irq_handler(struct regs *r) {

	debug_print(E1000_LOG_LEVEL, "RECEIVED INTERRUPT FROM E1000");

	uint32_t status = read_command(0xc0);

	irq_ack(e1000_irq);

	if (!status) {
		return 0;
	}

	if (status & 0x04) {
		/* Start link */
		debug_print(E1000_LOG_LEVEL, "start link");
	} else if (status & 0x10) {
		/* ?? */
	} else if (status & ((1 << 6) | (1 << 7))) {
		/* receive packet */
		do {
			rx_index = read_command(E1000_REG_RXDESCTAIL);
			if (rx_index == (int)read_command(E1000_REG_RXDESCHEAD)) return 1;
			rx_index = (rx_index + 1) % E1000_NUM_RX_DESC;
			if (rx[rx_index].status & 0x01) {
				uint8_t * pbuf = (uint8_t *)rx_virt[rx_index];
				uint16_t  plen = rx[rx_index].length;

				void * packet = malloc(plen);
				memcpy(packet, pbuf, plen);

				rx[rx_index].status = 0;

				enqueue_packet(packet);

				write_command(E1000_REG_RXDESCTAIL, rx_index);
			} else {
				break;
			}
		} while (1);
		wakeup_queue(rx_wait);
	}

	return 1;
}

static void send_packet(uint8_t* payload, size_t payload_size) {
	tx_index = read_command(E1000_REG_TXDESCTAIL);
	debug_print(E1000_LOG_LEVEL,"sending packet 0x%x, %d desc[%d]", payload, payload_size, tx_index);

	memcpy(tx_virt[tx_index], payload, payload_size);
	tx[tx_index].length = payload_size;
	tx[tx_index].cmd = CMD_EOP | CMD_IFCS | CMD_RS; //| CMD_RPS;
	tx[tx_index].status = 0;

	tx_index = (tx_index + 1) % E1000_NUM_TX_DESC;
	write_command(E1000_REG_TXDESCTAIL, tx_index);
}

static void init_rx(void) {

	write_command(E1000_REG_RXDESCLO, rx_phys);
	write_command(E1000_REG_RXDESCHI, 0);

	write_command(E1000_REG_RXDESCLEN, E1000_NUM_RX_DESC * sizeof(struct rx_desc));

	write_command(E1000_REG_RXDESCHEAD, 0);
	write_command(E1000_REG_RXDESCTAIL, E1000_NUM_RX_DESC - 1);

	rx_index = 0;

	write_command(E1000_REG_RCTRL,
		RCTL_EN  |
		(read_command(E1000_REG_RCTRL) & (~((1 << 17) | (1 << 16)))));

}

static void init_tx(void) {


	write_command(E1000_REG_TXDESCLO, tx_phys);
	write_command(E1000_REG_TXDESCHI, 0);

	write_command(E1000_REG_TXDESCLEN, E1000_NUM_TX_DESC * sizeof(struct tx_desc));

	write_command(E1000_REG_TXDESCHEAD, 0);
	write_command(E1000_REG_TXDESCTAIL, 0);

	tx_index = 0;

	write_command(E1000_REG_TCTRL,
		TCTL_EN |
		TCTL_PSP |
		read_command(E1000_REG_TCTRL));
}


static void e1000_init(void * data, char * name) {

	debug_print(E1000_LOG_LEVEL, "enabling bus mastering");
	uint16_t command_reg = pci_read_field(e1000_device_pci, PCI_COMMAND, 2);
	command_reg |= (1 << 2);
	command_reg |= (1 << 0);
	pci_write_field(e1000_device_pci, PCI_COMMAND, 2, command_reg);

	debug_print(E1000_LOG_LEVEL, "mem base: 0x%x", mem_base);

	eeprom_detect();
	debug_print(E1000_LOG_LEVEL, "has_eeprom = %d", has_eeprom);
	read_mac();

	debug_print(E1000_LOG_LEVEL, "device mac %2x:%2x:%2x:%2x:%2x:%2x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	unsigned long s, ss;

	uint32_t ctrl = read_command(E1000_REG_CTRL);
	/* reset phy */
	write_command(E1000_REG_CTRL, ctrl | (0x80000000));
	read_command(E1000_REG_STATUS);
	relative_time(0, 10, &s, &ss);
	sleep_until((process_t *)current_process, s, ss);
	switch_task(0);

	/* reset mac */
	write_command(E1000_REG_CTRL, ctrl | (0x04000000));
	read_command(E1000_REG_STATUS);
	relative_time(0, 10, &s, &ss);
	sleep_until((process_t *)current_process, s, ss);
	switch_task(0);

	/* Reload EEPROM */
	write_command(E1000_REG_CTRL, ctrl | (0x00002000));
	read_command(E1000_REG_STATUS);
	relative_time(0, 20, &s, &ss);
	sleep_until((process_t *)current_process, s, ss);
	switch_task(0);


	/* initialize */
	write_command(E1000_REG_CTRL, ctrl | (1 << 26));

	/* wait */
	relative_time(0, 10, &s, &ss);
	sleep_until((process_t *)current_process, s, ss);
	switch_task(0);
	debug_print(E1000_LOG_LEVEL, "back from sleep");

	uint32_t status = read_command(E1000_REG_CTRL);
	status |= (1 << 5);   /* set auto speed detection */
	status |= (1 << 6);   /* set link up */
	status &= ~(1 << 3);  /* unset link reset */
	status &= ~(1UL << 31UL); /* unset phy reset */
	status &= ~(1 << 7);  /* unset invert loss-of-signal */
	write_command(E1000_REG_CTRL, status);

	/* Disables flow control */
	write_command(0x0028, 0);
	write_command(0x002c, 0);
	write_command(0x0030, 0);
	write_command(0x0170, 0);

	/* Unset flow control */
	status = read_command(E1000_REG_CTRL);
	status &= ~(1 << 30);
	write_command(E1000_REG_CTRL, status);

	relative_time(0, 10, &s, &ss);
	sleep_until((process_t *)current_process, s, ss);
	switch_task(0);

	net_queue = list_create();
	rx_wait = list_create();

	uint32_t irq_pin = pci_read_field(e1000_device_pci, 0x3D, 1);
	debug_print(E1000_LOG_LEVEL, "IRQ pin is 0x%2x", irq_pin);
	e1000_irq = pci_read_field(e1000_device_pci, PCI_INTERRUPT_LINE, 1);

#define REQ_IRQ 11
	if (e1000_irq == 255) {
		debug_print(E1000_LOG_LEVEL, "IRQ line is not set for E1000, trying 11");
		/* Bad interrupt, need to select one */
		e1000_irq = REQ_IRQ; /* seems to work okay */
		pci_write_field(e1000_device_pci, PCI_INTERRUPT_LINE, 1, e1000_irq);
		e1000_irq = pci_read_field(e1000_device_pci, PCI_INTERRUPT_LINE, 1);
		if (e1000_irq != REQ_IRQ) {
			debug_print(E1000_LOG_LEVEL, "irq 10 was rejected?");
		}
	}

	irq_install_handler(e1000_irq, irq_handler, "e1000");

	debug_print(E1000_LOG_LEVEL, "Binding interrupt %d", e1000_irq);

	for (int i = 0; i < 128; ++i) {
		write_command(0x5200 + i * 4, 0);
	}

	for (int i = 0; i < 64; ++i) {
		write_command(0x4000 + i * 4, 0);
	}

#if 0
	/* This would rewrite the MAC address... */
	write_command(0x5400, *(uint32_t*)(&mac[0]));
	write_command(0x5404, *(uint16_t*)(&mac[4]));
	write_command(0x5404, read_command(0x5404) | (1 << 31));
#endif

	write_command(E1000_REG_RCTRL, (1 << 4));

	init_rx();
	init_tx();

	/* Twiddle interrupts */
	write_command(0x00D0, 0xFF);
	write_command(0x00D8, 0xFF);
	write_command(0x00D0,(1 << 2) | (1 << 6) | (1 << 7) | (1 << 1) | (1 << 0));

	relative_time(0, 10, &s, &ss);
	sleep_until((process_t *)current_process, s, ss);
	switch_task(0);

	int link_is_up = (read_command(E1000_REG_STATUS) & (1 << 1));
	debug_print(E1000_LOG_LEVEL,"e1000 done. has_eeprom = %d, link is up = %d, irq=%d", has_eeprom, link_is_up, e1000_irq);

	init_netif_funcs(get_mac, dequeue_packet, send_packet, "Intel E1000");
}

static int init(void) {
	pci_scan(&find_e1000, -1, &e1000_device_pci);

	if (!e1000_device_pci) {
		debug_print(E1000_LOG_LEVEL, "No e1000 device found.");
		return 1;
	}

	/* This seems to always be memory mapped on important devices. */
	mem_base  = pci_read_field(e1000_device_pci, PCI_BAR0, 4) & 0xFFFFFFF0;

	for (size_t x = 0; x < 0x10000; x += 0x1000) {
		uintptr_t addr = (mem_base & 0xFFFFF000) + x;
		dma_frame(get_page(addr, 1, kernel_directory), 1, 1, addr);
	}

	rx = (void*)kvmalloc_p(sizeof(struct rx_desc) * E1000_NUM_RX_DESC + 16, &rx_phys);

	for (int i = 0; i < E1000_NUM_RX_DESC; ++i) {
		rx_virt[i] = (void*)kvmalloc_p(8192 + 16, (uint32_t *)&rx[i].addr);
		debug_print(E1000_LOG_LEVEL, "rx[%d] 0x%x → 0x%x", i, rx_virt[i], (uint32_t)rx[i].addr);
		rx[i].status = 0;
	}

	tx = (void*)kvmalloc_p(sizeof(struct tx_desc) * E1000_NUM_TX_DESC + 16, &tx_phys);

	for (int i = 0; i < E1000_NUM_TX_DESC; ++i) {
		tx_virt[i] = (void*)kvmalloc_p(8192+16, (uint32_t *)&tx[i].addr);
		debug_print(E1000_LOG_LEVEL, "tx[%d] 0x%x → 0x%x", i, tx_virt[i], (uint32_t)tx[i].addr);
		tx[i].status = 0;
		tx[i].cmd = (1 << 0);
	}


	create_kernel_tasklet(e1000_init, "[e1000]", NULL);

	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(e1000, init, fini);
MODULE_DEPENDS(net);
