/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
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

/* XXX move this to ipv4? */
extern size_t print_dns_name(fs_node_t * tty, struct dns_packet * dns, size_t offset);

static uint32_t rtl_device_pci = 0x00000000;

static void find_rtl(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	if ((vendorid == 0x10ec) && (deviceid == 0x8139)) {
		*((uint32_t *)extra) = device;
	}
}

#define RTL_PORT_MAC     0x00
#define RTL_PORT_MAR     0x08
#define RTL_PORT_TXSTAT  0x10
#define RTL_PORT_TXBUF   0x20
#define RTL_PORT_RBSTART 0x30
#define RTL_PORT_CMD     0x37
#define RTL_PORT_RXPTR   0x38
#define RTL_PORT_RXADDR  0x3A
#define RTL_PORT_IMR     0x3C
#define RTL_PORT_ISR     0x3E
#define RTL_PORT_TCR     0x40
#define RTL_PORT_RCR     0x44
#define RTL_PORT_RXMISS  0x4C
#define RTL_PORT_CONFIG  0x52

static list_t * net_queue = NULL;

static spin_lock_t net_queue_lock = { 0 };

static int rtl_irq = 0;
static uint32_t rtl_iobase = 0;
static uint8_t * rtl_rx_buffer;
static uint8_t * rtl_tx_buffer[5];
static uint8_t mac[6];

static uint8_t * last_packet = NULL;

static uintptr_t rtl_rx_phys;
static uintptr_t rtl_tx_phys[5];

static uint32_t cur_rx = 0;
static int dirty_tx = 0;
static int next_tx = 0;

static list_t * rx_wait;

static spin_lock_t _lock;
static int next_tx_buf(void) {
	int out;
	spin_lock(_lock);
	out = next_tx;
	next_tx++;
	if (next_tx == 4) {
		next_tx = 0;
	}
	spin_unlock(_lock);
	return out;
}

void* rtl_dequeue() {
	while (!net_queue->length) {
		sleep_on(rx_wait);
	}

	spin_lock(net_queue_lock);
	node_t * n = list_dequeue(net_queue);
	void* value = (struct ethernet_packet *)n->value;
	free(n);
	spin_unlock(net_queue_lock);

	return value;
}

void rtl_enqueue(void * buffer) {
	/* XXX size? source? */
	spin_lock(net_queue_lock);
	list_insert(net_queue, buffer);
	spin_unlock(net_queue_lock);
}

uint8_t* rtl_get_mac() {
	return mac;
}

void rtl_send_packet(uint8_t* payload, size_t payload_size) {
	int my_tx = next_tx_buf();
	memcpy(rtl_tx_buffer[my_tx], payload, payload_size);

	outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * my_tx, rtl_tx_phys[my_tx]);
	outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * my_tx, payload_size);
}

struct ethernet_packet* rtl_get_packet(void) {
	return (struct ethernet_packet*)rtl_dequeue();
}

static int rtl_irq_handler(struct regs *r) {
	uint16_t status = inports(rtl_iobase + RTL_PORT_ISR);
	if (!status) {
		return 0;
	}
	outports(rtl_iobase + RTL_PORT_ISR, status);

	irq_ack(rtl_irq);

	if (status & 0x01 || status & 0x02) {
		/* Receive */
		while((inportb(rtl_iobase + RTL_PORT_CMD) & 0x01) == 0) {
			int offset = cur_rx % 0x2000;

#if 0
			uint16_t buf_addr = inports(rtl_iobase + RTL_PORT_RXADDR);
			uint16_t buf_ptr  = inports(rtl_iobase + RTL_PORT_RXPTR);
			uint8_t  cmd      = inportb(rtl_iobase + RTL_PORT_CMD);
#endif

			uint32_t * buf_start = (uint32_t *)((uintptr_t)rtl_rx_buffer + offset);
			uint32_t rx_status = buf_start[0];
			int rx_size = rx_status >> 16;

			if (rx_status & (0x0020 | 0x0010 | 0x0004 | 0x0002)) {
				debug_print(WARNING, "rx error :(");
			} else {
				uint8_t * buf_8 = (uint8_t *)&(buf_start[1]);

				last_packet = malloc(rx_size);

				uintptr_t packet_end = (uintptr_t)buf_8 + rx_size;
				if (packet_end > (uintptr_t)rtl_rx_buffer + 0x2000) {
					size_t s = ((uintptr_t)rtl_rx_buffer + 0x2000) - (uintptr_t)buf_8;
					memcpy(last_packet, buf_8, s);
					memcpy((void *)((uintptr_t)last_packet + s), rtl_rx_buffer, rx_size - s);
				} else {
					memcpy(last_packet, buf_8, rx_size);
				}

				rtl_enqueue(last_packet);
			}

			cur_rx = (cur_rx + rx_size + 4 + 3) & ~3;
			outports(rtl_iobase + RTL_PORT_RXPTR, cur_rx - 16);
		}
		wakeup_queue(rx_wait);
	}

	if (status & 0x08 || status & 0x04) {
		unsigned int i = inportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * dirty_tx);
		(void)i;
		dirty_tx++;
		if (dirty_tx == 5) dirty_tx = 0;
	}

	return 1;
}

#if 0
static void rtl_netd(void * data, char * name) {
	fs_node_t * tty = data;

	{
		fprintf(tty, "Sending DNS query...\n");
		uint8_t queries[] = {
			3,'i','r','c',
			8,'f','r','e','e','n','o','d','e',
			3,'n','e','t',
			0,
			0x00, 0x01, /* A */
			0x00, 0x01, /* IN */
		};

		int my_tx = next_tx_buf();
		size_t packet_size = write_dns_packet(rtl_tx_buffer[my_tx], sizeof(queries), queries);

		outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * my_tx, rtl_tx_phys[my_tx]);
		outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * my_tx, packet_size);
	}

	sleep_on(rx_wait);
	parse_dns_response(tty, last_packet);

	{
		fprintf(tty, "Sending DNS query...\n");
		uint8_t queries[] = {
			7,'n','y','a','n','c','a','t',
			5,'d','a','k','k','o',
			2,'u','s',
			0,
			0x00, 0x01, /* A */
			0x00, 0x01, /* IN */
		};

		int my_tx = next_tx_buf();
		size_t packet_size = write_dns_packet(rtl_tx_buffer[my_tx], sizeof(queries), queries);

		outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * my_tx, rtl_tx_phys[my_tx]);
		outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * my_tx, packet_size);
	}

	sleep_on(rx_wait);
	parse_dns_response(tty, last_packet);

	seq_no = krand();

	{
		fprintf(tty, "Sending TCP syn\n");
		int my_tx = next_tx_buf();
		uint8_t payload[] = { 0 };
		size_t packet_size = write_tcp_packet(rtl_tx_buffer[my_tx], payload, 0, (TCP_FLAGS_SYN | DATA_OFFSET_5));

		outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * my_tx, rtl_tx_phys[my_tx]);
		outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * my_tx, packet_size);

		seq_no += 1;
		ack_no = 0;
	}

	{
		struct ethernet_packet * eth = net_receive();
		uint16_t eth_type = ntohs(eth->type);

		fprintf(tty, "Ethernet II, Src: (%2x:%2x:%2x:%2x:%2x:%2x), Dst: (%2x:%2x:%2x:%2x:%2x:%2x) [type=%4x)\n",
				eth->source[0], eth->source[1], eth->source[2],
				eth->source[3], eth->source[4], eth->source[5],
				eth->destination[0], eth->destination[1], eth->destination[2],
				eth->destination[3], eth->destination[4], eth->destination[5],
				eth_type);


		struct ipv4_packet * ipv4 = (struct ipv4_packet *)eth->payload;
		uint32_t src_addr = ntohl(ipv4->source);
		uint32_t dst_addr = ntohl(ipv4->destination);
		uint16_t length   = ntohs(ipv4->length);

		char src_ip[16];
		char dst_ip[16];

		ip_ntoa(src_addr, src_ip);
		ip_ntoa(dst_addr, dst_ip);

		fprintf(tty, "IP packet [%s → %s] length=%d bytes\n",
				src_ip, dst_ip, length);

		struct tcp_header * tcp = (struct tcp_header *)ipv4->payload;

		if (seq_no != ntohl(tcp->ack_number)) {
			fprintf(tty, "[eth] Expected ack number of 0x%x, got 0x%x\n",
					seq_no,
					ntohl(tcp->ack_number));
			fprintf(tty, "[eth] Bailing...\n");
			return;
		}

		ack_no = ntohl(tcp->seq_number) + 1;
		free(eth);
	}

	{
		fprintf(tty, "Sending TCP ack\n");
		int my_tx = next_tx_buf();
		uint8_t payload[] = { 0 };
		size_t packet_size = write_tcp_packet(rtl_tx_buffer[my_tx], payload, 0, (TCP_FLAGS_ACK | DATA_OFFSET_5));

		outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * my_tx, rtl_tx_phys[my_tx]);
		outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * my_tx, packet_size);
	}

	fprintf(tty, "[eth] s-next=0x%x, r-next=0x%x\n", seq_no, ack_no);

}
#endif

int init_rtl(void) {
	if (rtl_device_pci) {
		debug_print(NOTICE, "Located an RTL 8139: 0x%x\n", rtl_device_pci);

		uint16_t command_reg = pci_read_field(rtl_device_pci, PCI_COMMAND, 4);
		debug_print(NOTICE, "COMMAND register before: 0x%4x\n", command_reg);
		if (command_reg & (1 << 2)) {
			debug_print(NOTICE, "Bus mastering already enabled.\n");
		} else {
			command_reg |= (1 << 2); /* bit 2 */
			debug_print(NOTICE, "COMMAND register after:  0x%4x\n", command_reg);
			pci_write_field(rtl_device_pci, PCI_COMMAND, 4, command_reg);
			command_reg = pci_read_field(rtl_device_pci, PCI_COMMAND, 4);
			debug_print(NOTICE, "COMMAND register after:  0x%4x\n", command_reg);
		}

		rtl_irq = pci_read_field(rtl_device_pci, PCI_INTERRUPT_LINE, 1);
		debug_print(NOTICE, "Interrupt Line: %x\n", rtl_irq);
		irq_install_handler(rtl_irq, rtl_irq_handler);

		uint32_t rtl_bar0 = pci_read_field(rtl_device_pci, PCI_BAR0, 4);
		uint32_t rtl_bar1 = pci_read_field(rtl_device_pci, PCI_BAR1, 4);

		debug_print(NOTICE, "BAR0: 0x%8x\n", rtl_bar0);
		debug_print(NOTICE, "BAR1: 0x%8x\n", rtl_bar1);

		rtl_iobase = 0x00000000;

		if (rtl_bar0 & 0x00000001) {
			rtl_iobase = rtl_bar0 & 0xFFFFFFFC;
		} else {
			debug_print(NOTICE, "This doesn't seem right! RTL8139 should be using an I/O BAR; this looks like a memory bar.");
		}

		debug_print(NOTICE, "RTL iobase: 0x%x\n", rtl_iobase);

		rx_wait = list_create();

		debug_print(NOTICE, "Determining mac address...\n");
		for (int i = 0; i < 6; ++i) {
			mac[i] = inports(rtl_iobase + RTL_PORT_MAC + i);
		}

		debug_print(NOTICE, "%2x:%2x:%2x:%2x:%2x:%2x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

		debug_print(NOTICE, "Enabling RTL8139.\n");
		outportb(rtl_iobase + RTL_PORT_CONFIG, 0x0);

		debug_print(NOTICE, "Resetting RTL8139.\n");
		outportb(rtl_iobase + RTL_PORT_CMD, 0x10);
		while ((inportb(rtl_iobase + 0x37) & 0x10) != 0) { }

		debug_print(NOTICE, "Done resetting RTL8139.\n");

		for (int i = 0; i < 5; ++i) {
			rtl_tx_buffer[i] = (void*)kvmalloc_p(0x1000, &rtl_tx_phys[i]);
			for (int j = 0; j < 60; ++j) {
				rtl_tx_buffer[i][j] = 0xF0;
			}
		}

		rtl_rx_buffer = (uint8_t *)kvmalloc_p(0x3000, &rtl_rx_phys);
		memset(rtl_rx_buffer, 0x00, 0x3000);

		debug_print(NOTICE, "Buffers:\n");
		debug_print(NOTICE, "   rx 0x%x [phys 0x%x and 0x%x and 0x%x]\n", rtl_rx_buffer, rtl_rx_phys, map_to_physical((uintptr_t)rtl_rx_buffer + 0x1000), map_to_physical((uintptr_t)rtl_rx_buffer + 0x2000));

		for (int i = 0; i < 5; ++i) {
			debug_print(NOTICE, "   tx 0x%x [phys 0x%x]\n", rtl_tx_buffer[i], rtl_tx_phys[i]);
		}

		debug_print(NOTICE, "Initializing receive buffer.\n");
		outportl(rtl_iobase + RTL_PORT_RBSTART, rtl_rx_phys);

		debug_print(NOTICE, "Enabling IRQs.\n");
		outports(rtl_iobase + RTL_PORT_IMR,
			0x8000 | /* PCI error */
			0x4000 | /* PCS timeout */
			0x40   | /* Rx FIFO over */
			0x20   | /* Rx underrun */
			0x10   | /* Rx overflow */
			0x08   | /* Tx error */
			0x04   | /* Tx okay */
			0x02   | /* Rx error */
			0x01     /* Rx okay */
		); /* TOK, ROK */

		debug_print(NOTICE, "Configuring transmit\n");
		outportl(rtl_iobase + RTL_PORT_TCR,
			0
		);

		debug_print(NOTICE, "Configuring receive buffer.\n");
		outportl(rtl_iobase + RTL_PORT_RCR,
			(0)       | /* 8K receive */
			0x08      | /* broadcast */
			0x01        /* all physical */
		);

		debug_print(NOTICE, "Enabling receive and transmit.\n");
		outportb(rtl_iobase + RTL_PORT_CMD, 0x08 | 0x04);

		debug_print(NOTICE, "Resetting rx stats\n");
		outportl(rtl_iobase + RTL_PORT_RXMISS, 0);

		net_queue = list_create();

#if 1
		{
			debug_print(NOTICE, "Sending DHCP discover\n");
			size_t packet_size = write_dhcp_packet(rtl_tx_buffer[next_tx]);

			outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * next_tx, rtl_tx_phys[next_tx]);
			outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * next_tx, packet_size);

			next_tx++;
			if (next_tx == 4) {
				next_tx = 0;
			}
		}

		{
			struct ethernet_packet * eth = (struct ethernet_packet *)rtl_dequeue();
			uint16_t eth_type = ntohs(eth->type);

			debug_print(NOTICE, "Ethernet II, Src: (%2x:%2x:%2x:%2x:%2x:%2x), Dst: (%2x:%2x:%2x:%2x:%2x:%2x) [type=%4x)\n",
					eth->source[0], eth->source[1], eth->source[2],
					eth->source[3], eth->source[4], eth->source[5],
					eth->destination[0], eth->destination[1], eth->destination[2],
					eth->destination[3], eth->destination[4], eth->destination[5],
					eth_type);


			struct ipv4_packet * ipv4 = (struct ipv4_packet *)eth->payload;
			uint32_t src_addr = ntohl(ipv4->source);
			uint32_t dst_addr = ntohl(ipv4->destination);
			uint16_t length   = ntohs(ipv4->length);

			char src_ip[16];
			char dst_ip[16];

			ip_ntoa(src_addr, src_ip);
			ip_ntoa(dst_addr, dst_ip);

			debug_print(NOTICE, "IP packet [%s → %s] length=%d bytes\n",
					src_ip, dst_ip, length);

			struct udp_packet * udp = (struct udp_packet *)ipv4->payload;;
			uint16_t src_port = ntohs(udp->source_port);
			uint16_t dst_port = ntohs(udp->destination_port);
			uint16_t udp_len  = ntohs(udp->length);

			debug_print(NOTICE, "UDP [%d → %d] length=%d bytes\n",
					src_port, dst_port, udp_len);

			struct dhcp_packet * dhcp = (struct dhcp_packet *)udp->payload;
			uint32_t yiaddr = ntohl(dhcp->yiaddr);

			char yiaddr_ip[16];
			ip_ntoa(yiaddr, yiaddr_ip);
			debug_print(NOTICE,  "DHCP Offer: %s\n", yiaddr_ip);

			free(eth);
		}

#endif

		debug_print(NOTICE, "Card is configured, going to start worker thread now.\n");

		debug_print(NOTICE, "Initializing netif functions\n");

		init_netif_funcs(rtl_get_mac, rtl_get_packet, rtl_send_packet);
		create_kernel_tasklet(net_handler, "[eth]", NULL);

		debug_print(NOTICE, "Back from starting the worker thread.\n");
	} else {
		return -1;
	}
	return 0;
}

static int init(void) {
	pci_scan(&find_rtl, -1, &rtl_device_pci);
	if (!rtl_device_pci) {
		debug_print(ERROR, "No RTL 8139 found?");
		return 1;
	}
	init_rtl();
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(rtl, init, fini);
MODULE_DEPENDS(net);
