/**
 * @file kernel/net/e1000.c
 * @brief Intel Gigabit Ethernet device driver
 * @package x86_64
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2017-2021 K. Lange
 *
 * @ref https://www.intel.com/content/dam/www/public/us/en/documents/manuals/pcie-gbe-controllers-open-source-manual.pdf
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

#define INTS (ICR_LSC | ICR_RXO | ICR_RXT0 | ICR_TXQE | ICR_TXDW | ICR_ACK | ICR_RXDMT0 | ICR_SRPD)

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

	spin_lock_t tx_lock;

	uint8_t * rx_virt[E1000_NUM_RX_DESC];
	uint8_t * tx_virt[E1000_NUM_TX_DESC];
	struct e1000_rx_desc * rx;
	struct e1000_tx_desc * tx;
	uintptr_t rx_phys;
	uintptr_t tx_phys;

	int configured;
	process_t * queuer;
	process_t * processor;

	netif_counters_t counts;
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

static int eeprom_detect(struct e1000_nic * device) {

	/* Definitely not */
	if (device->deviceid == 0x10d3) return 0;

	write_command(device, E1000_REG_EEPROM, 1);

	for (int i = 0; i < 10000 && !device->has_eeprom; ++i) {
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

static void e1000_handle(struct e1000_nic * nic, uint32_t status) {
	write_command(nic, E1000_REG_ICR, status);

	if (!nic->configured) {
		return;
	}

	if (status & ICR_LSC) {
		nic->link_status= (read_command(nic, E1000_REG_STATUS) & (1 << 1));
	}

	make_process_ready(nic->queuer);
}

static void e1000_queuer(void * data) {
	struct e1000_nic * nic = data;

	int head = read_command(nic, E1000_REG_RXDESCHEAD);
	int budget = 8;

	while (1) {
		int processed = 0;
		if (head == nic->rx_index) {
			head = read_command(nic, E1000_REG_RXDESCHEAD);
		}
		if (head != nic->rx_index) {
			while ((nic->rx[nic->rx_index].status & 0x01) && (processed < budget)) {
				int i = nic->rx_index;
				if (!(nic->rx[i].errors & (0x97))) {
					nic->counts.rx_count++;
					nic->counts.rx_bytes += nic->rx[i].length;
					net_eth_handle((void*)nic->rx_virt[i], nic->eth.device_node, nic->rx[i].length);
				} else {
					printf("error bits set in packet: %x\n", nic->rx[i].errors);
				}
				processed++;
				nic->rx[i].status = 0;
				if (++nic->rx_index == E1000_NUM_RX_DESC) {
					nic->rx_index = 0;
				}
				if (nic->rx_index == head) {
					head = read_command(nic, E1000_REG_RXDESCHEAD);
					if (nic->rx_index == head) break;
				}
				write_command(nic, E1000_REG_RXDESCTAIL, nic->rx_index);
				read_command(nic, E1000_REG_STATUS);
			}
		}
		if (processed == 0) {
			switch_task(0);
		} else {
			if (this_core->cpu_id == 0) switch_task(0);
		}
	}
}

static int irq_handler(struct regs *r) {
	int irq = r->int_no - 32;
	int handled = 0;

	for (int i = 0; i < device_count; ++i) {
		if (devices[i]->irq_number == irq) {
			uint32_t status = read_command(devices[i], E1000_REG_ICR);
			if (status) {
				e1000_handle(devices[i], status);
				if (!handled) {
					handled = 1;
					irq_ack(irq);
				}
			}
		}
	}

	return handled;
}

static int tx_full(struct e1000_nic * device, int tx_tail, int tx_head) {
	if (tx_tail == tx_head) return 0;
	if (device->tx_index == tx_head) return 1;
	if (((device->tx_index + 1) & E1000_NUM_TX_DESC) == tx_head) return 1;
	return 0;
}

static void send_packet(struct e1000_nic * device, uint8_t* payload, size_t payload_size) {
	spin_lock(device->tx_lock);
	int tx_tail = read_command(device, E1000_REG_TXDESCTAIL);
	int tx_head = read_command(device, E1000_REG_TXDESCHEAD);

	if (tx_full(device, tx_tail, tx_head)) {
		int timeout = 1000;
		do {
			spin_unlock(device->tx_lock);
			delay_yield(10000);
			timeout--;
			if (timeout == 0) {
				printf("e1000: wait for tx timed out, giving up\n");
				return;
			}
			spin_lock(device->tx_lock);
			tx_tail = read_command(device, E1000_REG_TXDESCTAIL);
			tx_head = read_command(device, E1000_REG_TXDESCHEAD);
		} while (tx_full(device, tx_tail, tx_head));
	}

	memcpy(device->tx_virt[device->tx_index], payload, payload_size);
	device->tx[device->tx_index].length = payload_size;
	device->tx[device->tx_index].cmd = CMD_EOP | CMD_IFCS | CMD_RS; //| CMD_RPS;
	device->tx[device->tx_index].status = 0;

	device->counts.tx_count++;
	device->counts.tx_bytes += payload_size;

	if (++device->tx_index == E1000_NUM_TX_DESC) {
		device->tx_index = 0;
	}
	write_command(device, E1000_REG_TXDESCTAIL, device->tx_index);
	read_command(device, E1000_REG_STATUS);

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
		(1 << 25) | /* Extended size... */
		(3 << 16) | /*   4096 */
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

	uint32_t tctl = read_command(device, E1000_REG_TCTRL);

	/* Collision threshold */
	tctl &= ~(0xFF << 4);
	tctl |= (15 << 4);

	/* Turn it on */
	tctl |= TCTL_EN;
	tctl |= TCTL_PSP;
	tctl |= (1 << 24); /* retransmit on late collision */

	write_command(device, E1000_REG_TCTRL, tctl);
}

#define privileged() do { if (this_core->current_process->user != USER_ROOT_UID) { return -EPERM; } } while (0)

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
			privileged();
			memcpy(&nic->eth.ipv4_addr, argp, sizeof(nic->eth.ipv4_addr));
			return 0;
		case SIOCGIFNETMASK:
			if (nic->eth.ipv4_subnet == 0) return -ENOENT;
			memcpy(argp, &nic->eth.ipv4_subnet, sizeof(nic->eth.ipv4_subnet));
			return 0;
		case SIOCSIFNETMASK:
			privileged();
			memcpy(&nic->eth.ipv4_subnet, argp, sizeof(nic->eth.ipv4_subnet));
			return 0;
		case SIOCGIFGATEWAY:
			if (nic->eth.ipv4_subnet == 0) return -ENOENT;
			memcpy(argp, &nic->eth.ipv4_gateway, sizeof(nic->eth.ipv4_gateway));
			return 0;
		case SIOCSIFGATEWAY:
			privileged();
			memcpy(&nic->eth.ipv4_gateway, argp, sizeof(nic->eth.ipv4_gateway));
			net_arp_ask(nic->eth.ipv4_gateway, node);
			return 0;

		case SIOCGIFADDR6:
			return -ENOENT;
		case SIOCSIFADDR6:
			privileged();
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

		case SIOCGIFCOUNTS: {
			memcpy(argp, &nic->counts, sizeof(netif_counters_t));
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

static void ints_off(struct e1000_nic * nic) {
	write_command(nic, E1000_REG_IMC, 0xFFFFFFFF);
	write_command(nic, E1000_REG_ICR, 0xFFFFFFFF);
	read_command(nic, E1000_REG_STATUS);
}

static void e1000_init(struct e1000_nic * nic) {
	uint32_t e1000_device_pci = nic->pci_device;

	nic->rx_phys = mmu_allocate_n_frames(2) << 12;
	nic->rx = mmu_map_mmio_region(nic->rx_phys, 8192);

	nic->tx_phys = mmu_allocate_n_frames(2) << 12;
	nic->tx = mmu_map_mmio_region(nic->tx_phys, 8192);

	memset(nic->rx, 0, sizeof(struct e1000_rx_desc) * E1000_NUM_RX_DESC);
	memset(nic->tx, 0, sizeof(struct e1000_tx_desc) * E1000_NUM_TX_DESC);

	/* Allocate buffers */
	for (int i = 0; i < E1000_NUM_RX_DESC; ++i) {
		nic->rx[i].addr = mmu_allocate_a_frame() << 12;
		nic->rx_virt[i] = mmu_map_mmio_region(nic->rx[i].addr, 4096);
		mmu_frame_map_address(mmu_get_page((uintptr_t)nic->rx_virt[i],0),MMU_FLAG_KERNEL|MMU_FLAG_WRITABLE|MMU_FLAG_WC,nic->rx[i].addr);
		nic->rx[i].status = 0;
	}

	for (int i = 0; i < E1000_NUM_TX_DESC; ++i) {
		nic->tx[i].addr = mmu_allocate_a_frame() << 12;
		nic->tx_virt[i] = mmu_map_mmio_region(nic->tx[i].addr, 4096);
		mmu_frame_allocate(mmu_get_page((uintptr_t)nic->tx_virt[i],0),MMU_FLAG_KERNEL|MMU_FLAG_WRITABLE|MMU_FLAG_WC);
		memset(nic->tx_virt[i], 0, 4096);
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

	#define CTRL_PHY_RST (1UL << 31UL)
	#define CTRL_RST     (1UL << 26UL)
	#define CTRL_SLU     (1UL << 6UL)
	#define CTRL_LRST    (1UL << 3UL)

	nic->irq_number = pci_get_interrupt(e1000_device_pci);
	irq_install_handler(nic->irq_number, irq_handler, nic->eth.if_name);

	/* Disable interrupts */
	ints_off(nic);

	/* Turn off receive + transmit */
	write_command(nic, E1000_REG_RCTRL, 0);
	write_command(nic, E1000_REG_TCTRL, TCTL_PSP);
	read_command(nic, E1000_REG_STATUS);
	delay_yield(10000);

	/* Reset everything */
	uint32_t ctrl = read_command(nic, E1000_REG_CTRL);
	ctrl |= CTRL_RST;
	write_command(nic, E1000_REG_CTRL, ctrl);
	delay_yield(20000);

	/* Turn off interrupts _again_ */
	ints_off(nic);

	/* Recommended flow control settings? */
	write_command(nic, 0x0028, 0x002C8001);
	write_command(nic, 0x002c, 0x0100);
	write_command(nic, 0x0030, 0x8808);
	write_command(nic, 0x0170, 0xFFFF);

	/* Link up */
	uint32_t status = read_command(nic, E1000_REG_CTRL);
	status |= CTRL_SLU;
	status |= (2 << 8); /* Speed to gigabit... */
	status &= ~CTRL_LRST;
	status &= ~CTRL_PHY_RST;
	write_command(nic, E1000_REG_CTRL, status);

	/* Clear statistical counters */
	for (int i = 0; i < 128; ++i) {
		write_command(nic, 0x5200 + i * 4, 0);
	}

	for (int i = 0; i < 64; ++i) {
		read_command(nic, 0x4000 + i * 4);
	}

	init_rx(nic);
	init_tx(nic);

	write_command(nic, E1000_REG_RDTR, 0);
	write_command(nic, E1000_REG_ITR, 500);
	read_command(nic, E1000_REG_STATUS);

	nic->link_status = (read_command(nic, E1000_REG_STATUS) & (1 << 1));

	nic->eth.device_node = calloc(sizeof(fs_node_t),1);
	snprintf(nic->eth.device_node->name, 100, "%s", nic->eth.if_name);
	nic->eth.device_node->flags = FS_BLOCKDEVICE; /* NETDEVICE? */
	nic->eth.device_node->mask  = 0644; /* temporary; shouldn't be doing this with these device files */
	nic->eth.device_node->ioctl = ioctl_e1000;
	nic->eth.device_node->write = write_e1000;
	nic->eth.device_node->device = nic;

	nic->eth.mtu = 1500; /* guess */

	net_add_interface(nic->eth.if_name, nic->eth.device_node);

	char worker_name[34];
	snprintf(worker_name, 33, "[%s]", nic->eth.if_name);
	nic->queuer = spawn_worker_thread(e1000_queuer, worker_name, nic);

	nic->configured = 1;

	/* Twiddle interrupts */
	write_command(nic, E1000_REG_IMS, INTS);
	delay_yield(10000);
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

