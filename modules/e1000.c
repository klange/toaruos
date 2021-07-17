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
#include <kernel/net/eth.h>
#include <kernel/module.h>
#include <errno.h>

#include <kernel/arch/x86_64/irq.h>
#include <kernel/net/e1000.h>

#include <sys/socket.h>
#include <net/if.h>

#define INTS ((1 << 2) | (1 << 6) | (1 << 7) | (1 << 1) | (1 << 0))

struct e1000_nic {
	struct EthernetDevice eth;
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
	spin_lock_t tx_lock;
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
	memcpy(&low, &device->eth.mac[0], 4);
	memcpy(&high,&device->eth.mac[4], 2);
	memset((uint8_t *)&high + 2, 0, 2);
	high |= 0x80000000;
	write_command(device, E1000_REG_RXADDR + 0, low);
	write_command(device, E1000_REG_RXADDR + 4, high);
}

static void read_mac(struct e1000_nic * device) {
	if (device->has_eeprom) {
		uint32_t t;
		t = eeprom_read(device, 0);
		device->eth.mac[0] = t & 0xFF;
		device->eth.mac[1] = t >> 8;
		t = eeprom_read(device, 1);
		device->eth.mac[2] = t & 0xFF;
		device->eth.mac[3] = t >> 8;
		t = eeprom_read(device, 2);
		device->eth.mac[4] = t & 0xFF;
		device->eth.mac[5] = t >> 8;
	} else {
		uint32_t mac_addr_low  = *(uint32_t *)(device->mmio_addr + E1000_REG_RXADDR);
		uint32_t mac_addr_high = *(uint32_t *)(device->mmio_addr + E1000_REG_RXADDR + 4);
		device->eth.mac[0] = (mac_addr_low >> 0 ) & 0xFF;
		device->eth.mac[1] = (mac_addr_low >> 8 ) & 0xFF;
		device->eth.mac[2] = (mac_addr_low >> 16) & 0xFF;
		device->eth.mac[3] = (mac_addr_low >> 24) & 0xFF;
		device->eth.mac[4] = (mac_addr_high>> 0 ) & 0xFF;
		device->eth.mac[5] = (mac_addr_high>> 8 ) & 0xFF;
	}
}

static void e1000_alert_waiters(struct e1000_nic * nic) {
	spin_lock(nic->alert_lock);
	while (nic->alert_wait->head) {
		node_t * node = list_dequeue(nic->alert_wait);
		process_t * p = node->value;
		free(node);
		spin_unlock(nic->alert_lock);
		process_alert_node(p, nic->eth.device_node);
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

				void * packet = malloc(8192);
				if (plen > 8192) {
					printf("??? plen is too big\n");
				}
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
	spin_lock(device->tx_lock);
	device->tx_index = read_command(device, E1000_REG_TXDESCTAIL);

	memcpy(device->tx_virt[device->tx_index], payload, payload_size);
	device->tx[device->tx_index].length = payload_size;
	device->tx[device->tx_index].cmd = CMD_EOP | CMD_IFCS | CMD_RS; //| CMD_RPS;
	device->tx[device->tx_index].status = 0;

	device->tx_index = (device->tx_index + 1) % E1000_NUM_TX_DESC;
	write_command(device, E1000_REG_TXDESCTAIL, device->tx_index);
	spin_unlock(device->tx_lock);
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

extern void net_arp_ask(uint32_t addr, fs_node_t * fsnic);

static int ioctl_e1000(fs_node_t * node, unsigned long request, void * argp) {
	struct e1000_nic * nic = node->device;

	switch (request) {
		case SIOCGIFHWADDR:
			/* fill argp with mac */
			memcpy(argp, nic->eth.mac, 6);
			return 0;

		case SIOCGIFADDR:
			if (nic->eth.ipv4_addr == 0) return -ENOENT;
			memcpy(argp, &nic->eth.ipv4_addr, sizeof(nic->eth.ipv4_addr));
			return 0;
		case SIOCSIFADDR:
			memcpy(&nic->eth.ipv4_addr, argp, sizeof(nic->eth.ipv4_addr));
			return 0;
		case SIOCGIFNETMASK:
			if (nic->eth.ipv4_subnet == 0) return -ENOENT;
			memcpy(argp, &nic->eth.ipv4_subnet, sizeof(nic->eth.ipv4_subnet));
			return 0;
		case SIOCSIFNETMASK:
			memcpy(&nic->eth.ipv4_subnet, argp, sizeof(nic->eth.ipv4_subnet));
			return 0;
		case SIOCGIFGATEWAY:
			if (nic->eth.ipv4_subnet == 0) return -ENOENT;
			memcpy(argp, &nic->eth.ipv4_gateway, sizeof(nic->eth.ipv4_gateway));
			return 0;
		case SIOCSIFGATEWAY:
			memcpy(&nic->eth.ipv4_gateway, argp, sizeof(nic->eth.ipv4_gateway));
			net_arp_ask(nic->eth.ipv4_gateway, node);
			return 0;

		case SIOCGIFADDR6:
			return -ENOENT;
		case SIOCSIFADDR6:
			memcpy(&nic->eth.ipv6_addr, argp, sizeof(nic->eth.ipv6_addr));
			return 0;

		case SIOCGIFFLAGS: {
			uint32_t * flags = argp;
			*flags = IFF_RUNNING;
			if (nic->link_status) *flags |= IFF_UP;
			/* We turn these on in our init_tx */
			*flags |= IFF_BROADCAST;
			*flags |= IFF_MULTICAST;
			return 0;
		}

		case SIOCGIFMTU: {
			uint32_t * mtu = argp;
			*mtu = nic->eth.mtu;
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
	list_insert(((process_t *)process)->node_waits, nic->eth.device_node);
	spin_unlock(nic->alert_lock);
	return 0;
}

static void e1000_process(void * data) {
	struct e1000_nic * nic = data;
	while (1) {
		struct ethernet_packet * packet = dequeue_packet(nic);
		net_eth_handle(packet, nic->eth.device_node);
	}
}

static void e1000_init(struct e1000_nic * nic) {
	uint32_t e1000_device_pci = nic->pci_device;

	nic->rx_phys = mmu_allocate_a_frame() << 12;
	if (nic->rx_phys == 0) {
		printf("e1000[%s]: unable to allocate memory for buffers\n", nic->eth.if_name);
		switch_task(0);
	}
	nic->rx = mmu_map_from_physical(nic->rx_phys); //mmu_map_mmio_region(nic->rx_phys, 4096);
	nic->tx_phys = nic->rx_phys + 512;
	nic->tx = mmu_map_from_physical(nic->tx_phys); //mmu_map_mmio_region(nic->tx_phys, 4096);

	memset(nic->rx, 0, 4096);
	memset(nic->tx, 0, 4096);

	/* Allocate buffers */
	for (int i = 0; i < E1000_NUM_RX_DESC; ++i) {
		nic->rx[i].addr = mmu_allocate_n_frames(2) << 12;
		if (nic->rx[i].addr == 0) {
			printf("e1000[%s]: unable to allocate memory for receive buffer\n", nic->eth.if_name);
			switch_task(0);
		}
		//nic->rx_virt[i] = mmu_map_from_physical(nic->rx[i].addr);
		nic->rx_virt[i] = mmu_map_mmio_region(nic->rx[i].addr, 8192);
		mmu_frame_allocate(mmu_get_page((uintptr_t)nic->rx_virt[i],0),MMU_FLAG_WRITABLE|MMU_FLAG_WC);
		mmu_frame_allocate(mmu_get_page((uintptr_t)nic->rx_virt[i]+4096,0),MMU_FLAG_WRITABLE|MMU_FLAG_WC);
		nic->rx[i].status = 0;
	}

	for (int i = 0; i < E1000_NUM_TX_DESC; ++i) {
		nic->tx[i].addr = mmu_allocate_n_frames(2) << 12;
		if (nic->tx[i].addr == 0) {
			printf("e1000[%s]: unable to allocate memory for receive buffer\n", nic->eth.if_name);
			switch_task(0);
		}
		//nic->tx_virt[i] = mmu_map_from_physical(nic->tx[i].addr);
		nic->tx_virt[i] = mmu_map_mmio_region(nic->tx[i].addr, 8192);
		mmu_frame_allocate(mmu_get_page((uintptr_t)nic->tx_virt[i],0),MMU_FLAG_WRITABLE|MMU_FLAG_WC);
		mmu_frame_allocate(mmu_get_page((uintptr_t)nic->tx_virt[i]+4096,0),MMU_FLAG_WRITABLE|MMU_FLAG_WC);
		memset(nic->tx_virt[i], 0, 8192);
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

	irq_install_handler(nic->irq_number, irq_handler, nic->eth.if_name);

	for (int i = 0; i < 128; ++i) {
		write_command(nic, 0x5200 + i * 4, 0);
	}

	for (int i = 0; i < 64; ++i) {
		read_command(nic, 0x4000 + i * 4);
	}

	init_rx(nic);
	init_tx(nic);

	/* Twiddle interrupts */
	write_command(nic, 0x00D0, 0xFFFFFFFF);
	write_command(nic, 0x00D8, 0xFFFFFFFF);
	write_command(nic, 0x00D0, INTS);
	delay_yield(10000);

	nic->link_status = (read_command(nic, E1000_REG_STATUS) & (1 << 1));

	nic->eth.device_node = calloc(sizeof(fs_node_t),1);
	snprintf(nic->eth.device_node->name, 100, "%s", nic->eth.if_name);
	nic->eth.device_node->flags = FS_BLOCKDEVICE; /* NETDEVICE? */
	nic->eth.device_node->mask  = 0666; /* temporary; shouldn't be doing this with these device files */
	nic->eth.device_node->ioctl = ioctl_e1000;
	nic->eth.device_node->write = write_e1000;
	nic->eth.device_node->selectcheck = check_e1000;
	nic->eth.device_node->selectwait  = wait_e1000;
	nic->eth.device_node->device = nic;

	nic->eth.mtu = 1500; /* guess */

	net_add_interface(nic->eth.if_name, nic->eth.device_node);

	char worker_name[34];
	snprintf(worker_name, 33, "[%s]", nic->eth.if_name);
	spawn_worker_thread(e1000_process, worker_name, nic);
}

static void find_e1000(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * found) {
	if ((vendorid == 0x8086) && (deviceid == 0x100e || deviceid == 0x1004 || deviceid == 0x100f || deviceid == 0x10ea || deviceid == 0x10d3)) {
		/* Allocate a device */
		struct e1000_nic * nic = calloc(1,sizeof(struct e1000_nic));
		nic->pci_device = device;
		nic->deviceid   = deviceid;
		devices[device_count++] = nic;

		snprintf(nic->eth.if_name, 31,
			"enp%ds%d",
			(int)pci_extract_bus(device),
			(int)pci_extract_slot(device));

		e1000_init(nic);
		*(int*)found = 1;
	}
}

static int e1000_install(int argc, char * argv[]) {
	uint32_t found = 0;
	pci_scan(&find_e1000, -1, &found);

	if (!found) {
		/* TODO: Clean up? Remove ourselves? */
		return -ENODEV;
	}

	return 0;
}

static int fini(void) {
	/* TODO: Uninstall device */
	return 0;
}

struct Module metadata = {
	.name = "e1000",
	.init = e1000_install,
	.fini = fini,
};

