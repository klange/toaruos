#include <module.h>
#include <logging.h>
#include <printf.h>
#include <pci.h>
#include <mem.h>
#include <list.h>
#include <ipv4.h>
#include <mod/shell.h>

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

static int rtl_irq = 0;
static uint32_t rtl_iobase = 0;
static uint8_t * rtl_rx_buffer;
static uint8_t * rtl_tx_buffer[5];

static uint8_t * last_packet = NULL;

static uintptr_t rtl_rx_phys;
static uintptr_t rtl_tx_phys[5];

static uint32_t cur_rx = 0;
static int dirty_tx = 0;

static list_t * rx_wait;

static uint8_t _dhcp_packet[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0x08, 0x00, 0x45, 0x00,
	0x01, 0x10, 0x00, 0x01, 0x00, 0x00, 0x40, 0x11, 0x79, 0xDD, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
	0xFF, 0xFF, 0x00, 0x44, 0x00, 0x43, 0x00, 0xFC, 0x81, 0xCC, 0x01, 0x01, 0x06, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x52, 0x54, 0x00, 0x12, 0x34, 0x56, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x82, 0x53, 0x63, 0x35, 0x01, 0x01, 0xFF
};

static uint8_t _dns_packet[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x52, 0x54, 0x00, 0x12, 0x34, 0x56, 0x08, 0x00, 0x45, 0x00,
	0x00, 0x36, 0x00, 0x01, 0x00, 0x00, 0x40, 0x11, 0x62, 0xA5, 0x0A, 0x00, 0x02, 0x0F, 0x0A, 0x00,
	0x02, 0x03, 0x00, 0x35, 0x00, 0x35, 0x00, 0x22, 0x9E, 0x77, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x64, 0x61, 0x6B, 0x6B, 0x6F, 0x02, 0x75, 0x73, 0x00,
	0x00, 0x01, 0x00, 0x01
};

static void rtl_irq_handler(struct regs *r) {
	uint16_t status = inports(rtl_iobase + RTL_PORT_ISR);
	outports(rtl_iobase + RTL_PORT_ISR, status);

	irq_ack(rtl_irq);

	debug_print(NOTICE, "herp a derp");

	if (status & 0x01 || status & 0x02) {
		/* Receive */
		debug_print(NOTICE, "rx response");
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
				last_packet = buf_8;


				debug_print(NOTICE, "Some bytes from this packet: %2x%2x%2x%2x",
						buf_8[0],
						buf_8[1],
						buf_8[2],
						buf_8[3]);

			}

			cur_rx = (cur_rx + rx_size + 4 + 3) & ~3;
			outports(rtl_iobase + RTL_PORT_RXPTR, cur_rx - 16);
		}
		debug_print(NOTICE, "done processing receive");
		wakeup_queue(rx_wait);
	}

	if (status & 0x08 || status & 0x04) {
		debug_print(NOTICE,"tx response");
		unsigned int i = inportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * dirty_tx);
		debug_print(NOTICE, "Other bits: 0x%x; status=0x%x", i, status);
		dirty_tx++;
		if (dirty_tx == 5) dirty_tx = 0;
	}
}


DEFINE_SHELL_FUNCTION(rtl, "rtl8139 experiments") {
	if (rtl_device_pci) {
		fprintf(tty, "Located an RTL 8139: 0x%x\n", rtl_device_pci);

		uint16_t command_reg = pci_read_field(rtl_device_pci, PCI_COMMAND, 4);
		fprintf(tty, "COMMAND register before: 0x%4x\n", command_reg);
		if (command_reg & (1 << 2)) {
			fprintf(tty, "Bus mastering already enabled.\n");
		} else {
			command_reg |= (1 << 2); /* bit 2 */
			fprintf(tty, "COMMAND register after:  0x%4x\n", command_reg);
			pci_write_field(rtl_device_pci, PCI_COMMAND, 4, command_reg);
			command_reg = pci_read_field(rtl_device_pci, PCI_COMMAND, 4);
			fprintf(tty, "COMMAND register after:  0x%4x\n", command_reg);
		}

		rtl_irq = pci_read_field(rtl_device_pci, PCI_INTERRUPT_LINE, 1);

		fprintf(tty, "Interrupt Line: %x\n", rtl_irq);
		irq_install_handler(rtl_irq, rtl_irq_handler);

		uint32_t rtl_bar0 = pci_read_field(rtl_device_pci, PCI_BAR0, 4);
		uint32_t rtl_bar1 = pci_read_field(rtl_device_pci, PCI_BAR1, 4);

		fprintf(tty, "BAR0: 0x%8x\n", rtl_bar0);
		fprintf(tty, "BAR1: 0x%8x\n", rtl_bar1);

		rtl_iobase = 0x00000000;

		if (rtl_bar0 & 0x00000001) {
			rtl_iobase = rtl_bar0 & 0xFFFFFFFC;
		} else {
			fprintf(tty, "This doesn't seem right! RTL8139 should be using an I/O BAR; this looks like a memory bar.");
		}

		fprintf(tty, "RTL iobase: 0x%x\n", rtl_iobase);

		rx_wait = list_create();

		fprintf(tty, "Determining mac address...\n");

		uint8_t mac[6];
		for (int i = 0; i < 6; ++i) {
			mac[i] = inports(rtl_iobase + RTL_PORT_MAC + i);
			_dhcp_packet[6+i] = mac[i];
		}

		fprintf(tty, "%2x:%2x:%2x:%2x:%2x:%2x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

		fprintf(tty, "Enabling RTL8139.\n");
		outportb(rtl_iobase + RTL_PORT_CONFIG, 0x0);

		fprintf(tty, "Resetting RTL8139.\n");
		outportb(rtl_iobase + RTL_PORT_CMD, 0x10);
		while ((inportb(rtl_iobase + 0x37) & 0x10) != 0) { }

		fprintf(tty, "Done resetting RTL8139.\n");

		for (int i = 0; i < 5; ++i) {
			rtl_tx_buffer[i] = (void*)kvmalloc_p(0x1000, &rtl_tx_phys[i]);
			for (int j = 0; j < 60; ++j) {
				rtl_tx_buffer[i][j] = 0xF0;
			}
		}

		rtl_rx_buffer = (uint8_t *)kvmalloc_p(0x3000, &rtl_rx_phys);
		memset(rtl_rx_buffer, 0x00, 0x3000);

		fprintf(tty, "Buffers:\n");
		fprintf(tty, "   rx 0x%x [phys 0x%x and 0x%x and 0x%x]\n", rtl_rx_buffer, rtl_rx_phys, map_to_physical((uintptr_t)rtl_rx_buffer + 0x1000), map_to_physical((uintptr_t)rtl_rx_buffer + 0x2000));

		for (int i = 0; i < 5; ++i) {
			fprintf(tty, "   tx 0x%x [phys 0x%x]\n", rtl_tx_buffer[i], rtl_tx_phys[i]);
		}

		fprintf(tty, "Initializing receive buffer.\n");
		outportl(rtl_iobase + RTL_PORT_RBSTART, rtl_rx_phys);

		fprintf(tty, "Enabling IRQs.\n");
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

		fprintf(tty, "Configuring transmit\n");
		outportl(rtl_iobase + RTL_PORT_TCR,
			0
		);

		fprintf(tty, "Configuring receive buffer.\n");
		outportl(rtl_iobase + RTL_PORT_RCR,
			(0)       | /* 8K receive */
			0x08      | /* broadcast */
			0x01        /* all physical */
		);

		fprintf(tty, "Enabling receive and transmit.\n");
		outportb(rtl_iobase + RTL_PORT_CMD, 0x08 | 0x04);

		fprintf(tty, "Resetting rx stats\n");
		outportl(rtl_iobase + RTL_PORT_RXMISS, 0);

		fprintf(tty, "Sending DHCP discover\n");
		memcpy(rtl_tx_buffer[0], _dhcp_packet, sizeof(_dhcp_packet));
		outportl(rtl_iobase + RTL_PORT_TXBUF, rtl_tx_phys[0]);
		outportl(rtl_iobase + RTL_PORT_TXSTAT, sizeof(_dhcp_packet));

		sleep_on(rx_wait);

		fprintf(tty, "Awoken from sleep, checking receive buffer: %2x %2x %2x %2x\n",
			last_packet[0], last_packet[1], last_packet[2], last_packet[3]);

		/* Okay, going to evaluate some things */
		fprintf(tty, "DHCP Offer:  %d.%d.%d.%d\n",
				last_packet[0x3A],
				last_packet[0x3B],
				last_packet[0x3C],
				last_packet[0x3D]);

		fprintf(tty, "Sending DNS query...\n");
		memcpy(rtl_tx_buffer[1], _dns_packet, sizeof(_dns_packet));

		outportl(rtl_iobase + RTL_PORT_TXBUF+4, rtl_tx_phys[1]);
		outportl(rtl_iobase + RTL_PORT_TXSTAT+4, sizeof(_dns_packet));

		sleep_on(rx_wait);

		fprintf(tty, "Awoken from sleep, checking receive buffer: %2x %2x %2x %2x\n",
			last_packet[0], last_packet[1], last_packet[2], last_packet[3]);

		fprintf(tty, "dakko.us. = %d.%d.%d.%d\n",
				last_packet[0x50],
				last_packet[0x51],
				last_packet[0x52],
				last_packet[0x53]);

#if 0
		fprintf(tty, "Going to try to force-send a UDP packet...\n");
		struct ipv4_packet p;
		p.version_ihl = (4 << 4) & (5 << 0); /* IPv4, no options */
		p.dscp_ecn = 0; /* nope nope nope */
		p.length = sizeof(struct ipv4_packet) + sizeof(struct udp_packet) + sizeof(struct dhcp_packet);
		p.ident = 0;
		p.flags_fragment = 0;
		p.ttl = 0xFF;
		p.protocol = 17;
		p.checksum = 0; /* calculate this later */
		p.source = 0x00000000; /* 0.0.0.0 */
		p.destination = 0xFFFFFFFF; /* 255.255.255.255 */

		uint16_t * packet = (uint16_t *)&p;
		uint32_t total = 0;
		for (int i = 0; i < 10; ++i) {
			total += packet[i];
			if (total & 0x80000000) {
				total = (total & 0xFFFF) + (total >> 16);
			}
		}

		while (total >> 16) {
			total = (total & 0xFFFF) + (total >> 16);
		}

		p.checksum = ~total;

		struct udp_packet u;
		u.source = p.source;
		u.destination = p.destination;
		u.zeroes = 0;
		u.protocol = p.protocol;
		u.udp_length = p.length;
		u.source_port = 68;
		u.destination_port = 67;
		u.length = sizeof(struct dhcp_packet);
		u.checksum = 0;
#endif


	} else {
		return -1;
	}
	return 0;
}

static int init(void) {
	BIND_SHELL_FUNCTION(rtl);
	pci_scan(&find_rtl, -1, &rtl_device_pci);
	if (!rtl_device_pci) {
		debug_print(ERROR, "No RTL 8139 found?");
		return 1;
	}
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(rtl, init, fini);
MODULE_DEPENDS(debugshell);
