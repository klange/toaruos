/**
 * @file kernel/net/e1000.c
 * @brief Intel Gigabit Ethernet device driver
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2017-2021 K. Lange
 */
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>
#include <kernel/pipe.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/time.h>
#include <kernel/vfs.h>
#include <kernel/mod/net.h>
#include <kernel/net/netif.h>
#include <errno.h>

#include <kernel/arch/x86_64/irq.h>
#include <kernel/net/e1000.h>

#include <sys/socket.h>

#define INTS ((1 << 2) | (1 << 6) | (1 << 7) | (1 << 1) | (1 << 0))

struct e1000_nic {
	/* This should be generic netif struct stuff... */
	char if_name[32];
	uint8_t mac[6];

	size_t mtu;

	/* XXX: just to get things going */
	uint32_t ipv4_addr;
	uint32_t ipv4_subnet;

	uint8_t ipv6_addr[16];
	/* TODO: Address lists? */

	fs_node_t * device_node;
	uint32_t pci_device;
	uint16_t deviceid;
	uintptr_t mmio_addr;
	int irq_number;

	int has_eeprom;
	int rx_index;
	int tx_index;
	int link_status;

	spin_lock_t net_queue_lock;
	spin_lock_t alert_lock;
	list_t * net_queue;
	list_t * rx_wait;
	list_t * alert_wait;

	uint8_t * rx_virt[E1000_NUM_RX_DESC];
	uint8_t * tx_virt[E1000_NUM_TX_DESC];
	struct e1000_rx_desc * rx;
	struct e1000_tx_desc * tx;
	uintptr_t rx_phys;
	uintptr_t tx_phys;
};

static int device_count = 0;
static struct e1000_nic * devices[32] = {NULL};

static uint32_t mmio_read32(uintptr_t addr) {
	return *((volatile uint32_t*)(addr));
}
static void mmio_write32(uintptr_t addr, uint32_t val) {
	(*((volatile uint32_t*)(addr))) = val;
}

static void write_command(struct e1000_nic * device, uint16_t addr, uint32_t val) {
	mmio_write32(device->mmio_addr + addr, val);
}

static uint32_t read_command(struct e1000_nic * device, uint16_t addr) {
	return mmio_read32(device->mmio_addr + addr);
}

static void delay_yield(size_t subticks) {
	unsigned long s, ss;
	relative_time(0, subticks, &s, &ss);
	sleep_until((process_t *)this_core->current_process, s, ss);
	switch_task(0);
}

static void enqueue_packet(struct e1000_nic * device, void * buffer) {
	spin_lock(device->net_queue_lock);
	list_insert(device->net_queue, buffer);
	spin_unlock(device->net_queue_lock);
}

static struct ethernet_packet * dequeue_packet(struct e1000_nic * device) {
	while (!device->net_queue->length) {
		sleep_on(device->rx_wait);
	}

	spin_lock(device->net_queue_lock);
	node_t * n = list_dequeue(device->net_queue);
	void* value = n->value;
	free(n);
	spin_unlock(device->net_queue_lock);

	return value;
}

static int eeprom_detect(struct e1000_nic * device) {

	/* Definitely not */
	if (device->deviceid == 0x10d3) return 0;

	write_command(device, E1000_REG_EEPROM, 1);

	for (int i = 0; i < 100000 && !device->has_eeprom; ++i) {
		uint32_t val = read_command(device, E1000_REG_EEPROM);
		if (val & 0x10) device->has_eeprom = 1;
	}

	return 0;
}

static uint16_t eeprom_read(struct e1000_nic * device, uint8_t addr) {
	uint32_t temp = 0;
	write_command(device, E1000_REG_EEPROM, 1 | ((uint32_t)(addr) << 8));
	while (!((temp = read_command(device, E1000_REG_EEPROM)) & (1 << 4)));
	return (uint16_t)((temp >> 16) & 0xFFFF);
}

static void write_mac(struct e1000_nic * device) {
	uint32_t low, high;
	memcpy(&low, &device->mac[0], 4);
	memcpy(&high,&device->mac[4], 2);
	memset((uint8_t *)&high + 2, 0, 2);
	high |= 0x80000000;
	write_command(device, E1000_REG_RXADDR + 0, low);
	write_command(device, E1000_REG_RXADDR + 4, high);
}

static void read_mac(struct e1000_nic * device) {
	if (device->has_eeprom) {
		uint32_t t;
		t = eeprom_read(device, 0);
		device->mac[0] = t & 0xFF;
		device->mac[1] = t >> 8;
		t = eeprom_read(device, 1);
		device->mac[2] = t & 0xFF;
		device->mac[3] = t >> 8;
		t = eeprom_read(device, 2);
		device->mac[4] = t & 0xFF;
		device->mac[5] = t >> 8;
	} else {
		uint32_t mac_addr_low  = *(uint32_t *)(device->mmio_addr + E1000_REG_RXADDR);
		uint32_t mac_addr_high = *(uint32_t *)(device->mmio_addr + E1000_REG_RXADDR + 4);
		device->mac[0] = (mac_addr_low >> 0 ) & 0xFF;
		device->mac[1] = (mac_addr_low >> 8 ) & 0xFF;
		device->mac[2] = (mac_addr_low >> 16) & 0xFF;
		device->mac[3] = (mac_addr_low >> 24) & 0xFF;
		device->mac[4] = (mac_addr_high>> 0 ) & 0xFF;
		device->mac[5] = (mac_addr_high>> 8 ) & 0xFF;
	}
}

static void e1000_alert_waiters(struct e1000_nic * nic) {
	spin_lock(nic->alert_lock);
	while (nic->alert_wait->head) {
		node_t * node = list_dequeue(nic->alert_wait);
		process_t * p = node->value;
		free(node);
		spin_unlock(nic->alert_lock);
		process_alert_node(p, nic->device_node);
		spin_lock(nic->alert_lock);
	}
	spin_unlock(nic->alert_lock);
}

static void e1000_handle(struct e1000_nic * nic, uint32_t status) {
	if (status & ICR_LSC) {
		/* TODO: Change interface link status. */
		nic->link_status= (read_command(nic, E1000_REG_STATUS) & (1 << 1));
	}

	if (status & ICR_TXQE) {
		/* Transmit queue empty; nothing to do. */
	}

	if (status & ICR_TXDW) {
		/* transmit descriptor written */
	}

	if (status & (ICR_RXO | ICR_RXT0)) {
		/* Packet received. */
		do {
			nic->rx_index = read_command(nic, E1000_REG_RXDESCTAIL);
			if (nic->rx_index == (int)read_command(nic, E1000_REG_RXDESCHEAD)) return;
			nic->rx_index = (nic->rx_index + 1) % E1000_NUM_RX_DESC;
			if (nic->rx[nic->rx_index].status & 0x01) {
				uint8_t * pbuf = (uint8_t *)nic->rx_virt[nic->rx_index];
				uint16_t  plen = nic->rx[nic->rx_index].length;

				void * packet = malloc(8092);
				memcpy(packet, pbuf, plen);

				nic->rx[nic->rx_index].status = 0;

				enqueue_packet(nic, packet);

				write_command(nic, E1000_REG_RXDESCTAIL, nic->rx_index);
			} else {
				break;
			}
		} while (1);
		wakeup_queue(nic->rx_wait);
		e1000_alert_waiters(nic);
	}
}

static int irq_handler(struct regs *r) {
	int irq = r->int_no - 32;
	int handled = 0;

	for (int i = 0; i < device_count; ++i) {
		if (devices[i]->irq_number == irq) {
			uint32_t status = read_command(devices[i], E1000_REG_ICR);
			if (status) {
				write_command(devices[i], 0x00D8,INTS);
				e1000_handle(devices[i], status);
				read_command(devices[i], E1000_REG_ICR);
				if (!handled) {
					handled = 1;
					irq_ack(irq);
				}
				write_command(devices[i], 0x00D0,INTS);
			}
		}
	}

	return handled;
}

static void send_packet(struct e1000_nic * device, uint8_t* payload, size_t payload_size) {
	device->tx_index = read_command(device, E1000_REG_TXDESCTAIL);

	memcpy(device->tx_virt[device->tx_index], payload, payload_size);
	device->tx[device->tx_index].length = payload_size;
	device->tx[device->tx_index].cmd = CMD_EOP | CMD_IFCS | CMD_RS; //| CMD_RPS;
	device->tx[device->tx_index].status = 0;

	device->tx_index = (device->tx_index + 1) % E1000_NUM_TX_DESC;
	write_command(device, E1000_REG_TXDESCTAIL, device->tx_index);
}

static void init_rx(struct e1000_nic * device) {
	write_command(device, E1000_REG_RXDESCLO, device->rx_phys);
	write_command(device, E1000_REG_RXDESCHI, 0);
	write_command(device, E1000_REG_RXDESCLEN, E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));
	write_command(device, E1000_REG_RXDESCHEAD, 0);
	write_command(device, E1000_REG_RXDESCTAIL, E1000_NUM_RX_DESC - 1);

	device->rx_index = 0;

	write_command(device, E1000_REG_RCTRL,
		RCTL_EN  |
		(1 << 2) | /* store bad packets */
		(1 << 4) | /* multicast promiscuous */
		(1 << 15) | /* broadcast accept */
		(1 << 26) /* strip CRC */
	);
}

static void init_tx(struct e1000_nic * device) {
	write_command(device, E1000_REG_TXDESCLO, device->tx_phys);
	write_command(device, E1000_REG_TXDESCHI, 0);
	write_command(device, E1000_REG_TXDESCLEN, E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
	write_command(device, E1000_REG_TXDESCHEAD, 0);
	write_command(device, E1000_REG_TXDESCTAIL, 0);

	device->tx_index = 0;

	write_command(device, E1000_REG_TCTRL,
		TCTL_EN |
		TCTL_PSP |
		read_command(device, E1000_REG_TCTRL));
}

static int ioctl_e1000(fs_node_t * node, unsigned long request, void * argp) {
	struct e1000_nic * nic = node->device;

	switch (request) {
		case 0x12340001:
			/* fill argp with mac */
			memcpy(argp, nic->mac, 6);
			return 0;

		case 0x12340002:
			if (nic->ipv4_addr == 0) return -ENOENT;
			memcpy(argp, &nic->ipv4_addr, sizeof(nic->ipv4_addr));
			return 0;
		case 0x12340012:
			memcpy(&nic->ipv4_addr, argp, sizeof(nic->ipv4_addr));
			return 0;
		case 0x12340004:
			if (nic->ipv4_subnet == 0) return -ENOENT;
			memcpy(argp, &nic->ipv4_subnet, sizeof(nic->ipv4_subnet));
			return 0;
		case 0x12340014:
			memcpy(&nic->ipv4_subnet, argp, sizeof(nic->ipv4_subnet));
			return 0;

		case 0x12340003:
			return -ENOENT;
		case 0x12340013:
			memcpy(&nic->ipv6_addr, argp, sizeof(nic->ipv6_addr));
			return 0;

		case 0x12340005: {
			uint32_t * flags = argp;
			*flags = IFF_RUNNING;
			if (nic->link_status) *flags |= IFF_UP;
			/* We turn these on in our init_tx */
			*flags |= IFF_BROADCAST;
			*flags |= IFF_MULTICAST;
			return 0;
		}

		case 0x12340006: {
			uint32_t * mtu = argp;
			*mtu = nic->mtu;
			return 0;
		}

		case 0x123400FF: {
			/* discard all */
			spin_lock(nic->net_queue_lock);
			while (nic->net_queue->length) {
				node_t * n = list_dequeue(nic->net_queue);
				free(n->value);
				free(n);
			}
			spin_unlock(nic->net_queue_lock);
			return 0;
		}

		default:
			return -EINVAL;
	}
}

static ssize_t write_e1000(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	struct e1000_nic * nic = node->device;
	/* write packet */
	send_packet(nic, buffer, size);
	return size;
}

static ssize_t read_e1000(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	if (size != 8092) return 0;
	struct e1000_nic * nic = node->device;

	struct ethernet_packet * packet = dequeue_packet(nic);
	memcpy(buffer, packet, 8092);
	free(packet);

	return 8092;
}

static int check_e1000(fs_node_t *node) {
	struct e1000_nic * nic = node->device;
	return nic->net_queue->head ? 0 : 1;
}

static int wait_e1000(fs_node_t *node, void * process) {
	struct e1000_nic * nic = node->device;
	spin_lock(nic->alert_lock);
	if (!list_find(nic->alert_wait, process)) {
		list_insert(nic->alert_wait, process);
	}
	list_insert(((process_t *)process)->node_waits, nic->device_node);
	spin_unlock(nic->alert_lock);
	return 0;
}

static void e1000_init(void * data) {
	struct e1000_nic * nic = data;
	uint32_t e1000_device_pci = nic->pci_device;

	nic->rx_phys = mmu_allocate_a_frame() << 12;
	if (nic->rx_phys == 0) {
		printf("e1000[%s]: unable to allocate memory for buffers\n", nic->if_name);
		switch_task(0);
	}
	nic->rx = mmu_map_from_physical(nic->rx_phys);
	nic->tx_phys = nic->rx_phys + 512;
	nic->tx = mmu_map_from_physical(nic->tx_phys);

	/* Allocate buffers */
	for (int i = 0; i < E1000_NUM_RX_DESC; ++i) {
		nic->rx[i].addr = mmu_allocate_n_frames(2) << 12;
		if (nic->rx[i].addr == 0) {
			printf("e1000[%s]: unable to allocate memory for receive buffer\n", nic->if_name);
			switch_task(0);
		}
		nic->rx_virt[i] = mmu_map_from_physical(nic->rx[i].addr);
		nic->rx[i].status = 0;
	}

	for (int i = 0; i < E1000_NUM_TX_DESC; ++i) {
		nic->tx[i].addr = mmu_allocate_n_frames(2) << 12;
		if (nic->tx[i].addr == 0) {
			printf("e1000[%s]: unable to allocate memory for receive buffer\n", nic->if_name);
			switch_task(0);
		}
		nic->tx_virt[i] = mmu_map_from_physical(nic->tx[i].addr);
		nic->tx[i].status = 0;
		nic->tx[i].cmd = (1 << 0);
	}

	uint16_t command_reg = pci_read_field(e1000_device_pci, PCI_COMMAND, 2);
	command_reg |= (1 << 2);
	command_reg |= (1 << 0);
	pci_write_field(e1000_device_pci, PCI_COMMAND, 2, command_reg);

	delay_yield(10000);

	/* Is this size enough? */
	uint32_t initial_bar = pci_read_field(e1000_device_pci, PCI_BAR0, 4);
	nic->mmio_addr = (uintptr_t)mmu_map_mmio_region(initial_bar, 0x8000);

	eeprom_detect(nic);
	read_mac(nic);
	write_mac(nic);
	uint32_t ctrl = read_command(nic, E1000_REG_CTRL);

	/* reset phy */
	write_command(nic, E1000_REG_CTRL, ctrl | (0x80000000));
	read_command(nic, E1000_REG_STATUS);
	delay_yield(10000);

	/* reset mac */
	write_command(nic, E1000_REG_CTRL, ctrl | (0x04000000));
	read_command(nic, E1000_REG_STATUS);
	delay_yield(10000);

	/* Reload EEPROM */
	write_command(nic, E1000_REG_CTRL, ctrl | (0x00002000));
	read_command(nic, E1000_REG_STATUS);
	delay_yield(20000);

	/* initialize */
	write_command(nic, E1000_REG_CTRL, ctrl | (1 << 26));
	delay_yield(10000);

	uint32_t status = read_command(nic, E1000_REG_CTRL);
	status |= (1 << 5);   /* set auto speed detection */
	status |= (1 << 6);   /* set link up */
	status &= ~(1 << 3);  /* unset link reset */
	status &= ~(1UL << 31UL); /* unset phy reset */
	status &= ~(1 << 7);  /* unset invert loss-of-signal */
	write_command(nic, E1000_REG_CTRL, status);

	/* Disables flow control */
	write_command(nic, 0x0028, 0);
	write_command(nic, 0x002c, 0);
	write_command(nic, 0x0030, 0);
	write_command(nic, 0x0170, 0);

	/* Unset flow control */
	status = read_command(nic, E1000_REG_CTRL);
	status &= ~(1 << 30);
	write_command(nic, E1000_REG_CTRL, status);
	delay_yield(10000);

	nic->net_queue = list_create("e1000 net queue", nic);
	nic->rx_wait = list_create("e1000 rx sem", nic);
	nic->alert_wait = list_create("e1000 select waiters", nic);

	nic->irq_number = pci_get_interrupt(e1000_device_pci);

	irq_install_handler(nic->irq_number, irq_handler, nic->if_name);

	for (int i = 0; i < 128; ++i) {
		write_command(nic, 0x5200 + i * 4, 0);
	}

	for (int i = 0; i < 64; ++i) {
		write_command(nic, 0x4000 + i * 4, 0);
	}

	init_rx(nic);
	init_tx(nic);

	/* Twiddle interrupts */
	write_command(nic, 0x00D0, 0xFFFFFFFF);
	write_command(nic, 0x00D8, 0xFFFFFFFF);
	write_command(nic, 0x00D0, INTS);
	delay_yield(10000);

	nic->link_status = (read_command(nic, E1000_REG_STATUS) & (1 << 1));

	nic->device_node = calloc(sizeof(fs_node_t),1);
	snprintf(nic->device_node->name, 100, "%s", nic->if_name);
	nic->device_node->flags = FS_BLOCKDEVICE; /* NETDEVICE? */
	nic->device_node->mask  = 0666; /* temporary; shouldn't be doing this with these device files */
	nic->device_node->ioctl = ioctl_e1000;
	nic->device_node->write = write_e1000;
	nic->device_node->read  = read_e1000;
	nic->device_node->selectcheck = check_e1000;
	nic->device_node->selectwait  = wait_e1000;
	nic->device_node->device = nic;

	nic->mtu = 1500; /* guess */

	net_add_interface(nic->if_name, nic->device_node);

	switch_task(0);
}

static void find_e1000(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * found) {
	if ((vendorid == 0x8086) && (deviceid == 0x100e || deviceid == 0x1004 || deviceid == 0x100f || deviceid == 0x10ea || deviceid == 0x10d3)) {
		/* Allocate a device */
		struct e1000_nic * nic = calloc(1,sizeof(struct e1000_nic));
		nic->pci_device = device;
		nic->deviceid   = deviceid;
		devices[device_count++] = nic;

		snprintf(nic->if_name, 31,
			"enp%ds%d",
			(int)pci_extract_bus(device),
			(int)pci_extract_slot(device));

		char worker_name[34];
		snprintf(worker_name, 33, "[%s]", nic->if_name);
		spawn_worker_thread(e1000_init, worker_name, nic);

		*(int*)found = 1;
	}
}

void e1000_initialize(void) {
	uint32_t found = 0;
	pci_scan(&find_e1000, -1, &found);

	if (!found) {
		/* TODO: Clean up? Remove ourselves? */
		return;
	}
}

