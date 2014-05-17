#include <module.h>
#include <logging.h>
#include <printf.h>
#include <pci.h>
#include <mem.h>
#include <list.h>
#include <ipv4.h>
#include <mod/shell.h>

#define htonl(l)  ( (((l) & 0xFF) << 24) | (((l) & 0xFF00) << 8) | (((l) & 0xFF0000) >> 8) | (((l) & 0xFF000000) >> 24))
#define htons(s)  ( (((s) & 0xFF) << 8) | (((s) & 0xFF00) >> 8) )
#define ntohl(l)  htonl((l))
#define ntohs(s)  htons((s))

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
static uint8_t mac[6];

static uint8_t * last_packet = NULL;

static uintptr_t rtl_rx_phys;
static uintptr_t rtl_tx_phys[5];

static uint32_t cur_rx = 0;
static int dirty_tx = 0;
static int next_tx = 0;

static list_t * rx_wait;

#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define IPV4_PROT_UDP 17
#define IPV4_PROT_TCP 6
#define DHCP_MAGIC 0x63825363

struct tcp_header {
	uint16_t source_port;
	uint16_t destination_port;

	uint32_t seq_number;
	uint32_t ack_number;

	uint16_t flags;
	uint16_t window_size;
	uint16_t checksum;
	uint16_t urgent;

	uint8_t  payload[];
} __attribute__((packed));

struct tcp_check_header {
	uint32_t source;
	uint32_t destination;
	uint8_t  zeros;
	uint8_t  protocol;
	uint16_t tcp_len;
	uint8_t  tcp_header[];
};

uint32_t ip_aton(const char * in) {
	char ip[16];
	char * c = ip;
	int out[4];
	char * i;
	memcpy(ip, in, strlen(in) < 15 ? strlen(in) + 1 : 15);
	ip[15] = '\0';

	i = (char *)lfind(c, '.');
	*i = '\0';
	out[0] = atoi(c);
	c += strlen(c) + 1;

	i = (char *)lfind(c, '.');
	*i = '\0';
	out[1] = atoi(c);
	c += strlen(c) + 1;

	i = (char *)lfind(c, '.');
	*i = '\0';
	out[2] = atoi(c);
	c += strlen(c) + 1;

	out[3] = atoi(c);

	return ((out[0] << 24) | (out[1] << 16) | (out[2] << 8) | (out[3]));
}

void ip_ntoa(uint32_t src_addr, char * out) {
	sprintf(out, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

uint16_t calculate_ipv4_checksum(struct ipv4_packet * p) {
	uint32_t sum = 0;
	uint16_t * s = (uint16_t *)p;

	/* TODO: Checksums for options? */
	for (int i = 0; i < 10; ++i) {
		sum += ntohs(s[i]);
	}

	if (sum > 0xFFFF) {
		sum = (sum >> 16) + (sum & 0xFFFF);
	}

	return ~(sum & 0xFFFF) & 0xFFFF;
}

uint16_t calculate_tcp_checksum(struct tcp_check_header * p, struct tcp_header * h, void * d, size_t d_words) {
	uint32_t sum = 0;
	uint16_t * s = (uint16_t *)p;

	/* TODO: Checksums for options? */
	for (int i = 0; i < 6; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	s = (uint16_t *)h;
	for (int i = 0; i < 10; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	s = (uint16_t *)d;
	for (unsigned int i = 0; i < d_words; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	return ~(sum & 0xFFFF) & 0xFFFF;
}

#define TCP_FLAGS_SYN (1 << 1)
#define TCP_FLAGS_ACK (1 << 4)
#define DATA_OFFSET_5 (0x5 << 12)

static uint32_t seq_no = 0xff0000;
static uint32_t ack_no = 0x0;
static size_t write_tcp_packet(uint8_t * buffer, uint8_t * payload, size_t payload_size, uint16_t flags) {
	size_t offset = 0;

	/* Then, let's write an ethernet frame */
	struct ethernet_packet eth_out = {
		.source = { mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] },
		.destination = BROADCAST_MAC,
		.type = htons(0x0800),
	};

	memcpy(&buffer[offset], &eth_out, sizeof(struct ethernet_packet));
	offset += sizeof(struct ethernet_packet);

	/* Prepare the IPv4 header */
	uint16_t _length = htons(sizeof(struct ipv4_packet) + sizeof(struct tcp_header) + payload_size);
	uint16_t _ident  = htons(1);

	struct ipv4_packet ipv4_out = {
		.version_ihl = ((0x4 << 4) | (0x5 << 0)), /* 4 = ipv4, 5 = no options */
		.dscp_ecn = 0, /* not setting either of those */
		.length = _length,
		.ident = _ident,
		.flags_fragment = 0,
		.ttl = 0x40,
		.protocol = IPV4_PROT_TCP,
		.checksum = 0, /* fill this in later */
		.source = htonl(ip_aton("10.0.2.15")),
		.destination = htonl(ip_aton("37.48.83.75")),
		//.destination = htonl(ip_aton("204.28.125.145")),
		//.destination = htonl(ip_aton("192.168.1.145")),
	};

	uint16_t checksum = calculate_ipv4_checksum(&ipv4_out);
	ipv4_out.checksum = htons(checksum);

	memcpy(&buffer[offset], &ipv4_out, sizeof(struct ipv4_packet));
	offset += sizeof(struct ipv4_packet);

	struct tcp_header tcp = {
		.source_port = htons(56667), /* Ephemeral port */
		.destination_port = htons(6667), /* IRC */
		.seq_number = htonl(seq_no),
		.ack_number = flags & (TCP_FLAGS_ACK) ? htonl(ack_no) : 0,
		.flags = htons(flags),
		.window_size = htons(1024),
		.checksum = 0,
		.urgent = 0,
	};

	struct tcp_check_header check_hd = {
		.source = ipv4_out.source,
		.destination = ipv4_out.destination,
		.zeros = 0,
		.protocol = 6,
		.tcp_len = htons(sizeof(tcp)+payload_size),
	};

	uint16_t dwords = payload_size / 2;
	if (dwords * 2 != payload_size) {
		dwords++;
	}

	uint16_t t = calculate_tcp_checksum(&check_hd, &tcp, payload, dwords);
	tcp.checksum = htons(t);

	memcpy(&buffer[offset], &tcp, sizeof(struct tcp_header));
	offset += sizeof(struct tcp_header);

	memcpy(&buffer[offset], payload, payload_size);
	offset += payload_size;

	return offset;
}

static size_t write_dhcp_packet(uint8_t * buffer) {
	size_t offset = 0;
	size_t payload_size = sizeof(struct dhcp_packet);

	/* First, let's figure out how big this is supposed to be... */

	uint8_t dhcp_options[] = {
		53, /* Message type */
		1,  /* Length: 1 */
		1,  /* Discover */
		255, /* END */
	};

	payload_size += sizeof(dhcp_options);

	/* Then, let's write an ethernet frame */
	struct ethernet_packet eth_out = {
		.source = { mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] },
		.destination = BROADCAST_MAC,
		.type = htons(0x0800),
	};

	memcpy(&buffer[offset], &eth_out, sizeof(struct ethernet_packet));
	offset += sizeof(struct ethernet_packet);

	/* Prepare the IPv4 header */
	uint16_t _length = htons(sizeof(struct ipv4_packet) + sizeof(struct udp_packet) + payload_size);
	uint16_t _ident  = htons(1);

	struct ipv4_packet ipv4_out = {
		.version_ihl = ((0x4 << 4) | (0x5 << 0)), /* 4 = ipv4, 5 = no options */
		.dscp_ecn = 0, /* not setting either of those */
		.length = _length,
		.ident = _ident,
		.flags_fragment = 0,
		.ttl = 0x40,
		.protocol = IPV4_PROT_UDP,
		.checksum = 0, /* fill this in later */
		.source = htonl(ip_aton("0.0.0.0")),
		.destination = htonl(ip_aton("255.255.255.255")), /* XXX need macros for this */
	};

	uint16_t checksum = calculate_ipv4_checksum(&ipv4_out);
	ipv4_out.checksum = htons(checksum);

	memcpy(&buffer[offset], &ipv4_out, sizeof(struct ipv4_packet));
	offset += sizeof(struct ipv4_packet);

	uint16_t _udp_source = htons(68);
	uint16_t _udp_destination = htons(67);
	uint16_t _udp_length = htons(sizeof(struct udp_packet) + payload_size);

	/* Now let's build a UDP packet */
	struct udp_packet udp_out = {
		.source_port = _udp_source,
		.destination_port = _udp_destination,
		.length = _udp_length,
		.checksum = 0,
	};

	/* XXX calculate checksum here */

	memcpy(&buffer[offset], &udp_out, sizeof(struct udp_packet));
	offset += sizeof(struct udp_packet);

	/* BOOTP headers */
	struct dhcp_packet bootp_out = {
		.op = 1,
		.htype = 1,
		.hlen = 6, /* mac address... */
		.hops = 0,
		.xid = htonl(0x1337), /* transaction id */
		.secs = 0,
		.flags = 0,

		.ciaddr = 0x000000,
		.yiaddr = 0x000000,
		.siaddr = 0x000000,
		.giaddr = 0x000000,

		.chaddr = {mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], 0x00},
		.sname = {0},
		.file = {0},
		.magic = htonl(DHCP_MAGIC),
	};

	memcpy(&buffer[offset], &bootp_out, sizeof(struct dhcp_packet));
	offset += sizeof(struct dhcp_packet);

	memcpy(&buffer[offset], &dhcp_options, sizeof(dhcp_options));
	offset += sizeof(dhcp_options);

	return offset;
}

static size_t write_dns_packet(uint8_t * buffer, size_t queries_len, uint8_t * queries) {
	size_t offset = 0;
	size_t payload_size = sizeof(struct dns_packet) + queries_len;

	/* Then, let's write an ethernet frame */
	struct ethernet_packet eth_out = {
		.source = { mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] },
		.destination = BROADCAST_MAC,
		.type = htons(0x0800),
	};

	memcpy(&buffer[offset], &eth_out, sizeof(struct ethernet_packet));
	offset += sizeof(struct ethernet_packet);

	/* Prepare the IPv4 header */
	uint16_t _length = htons(sizeof(struct ipv4_packet) + sizeof(struct udp_packet) + payload_size);
	uint16_t _ident  = htons(1);

	struct ipv4_packet ipv4_out = {
		.version_ihl = ((0x4 << 4) | (0x5 << 0)), /* 4 = ipv4, 5 = no options */
		.dscp_ecn = 0, /* not setting either of those */
		.length = _length,
		.ident = _ident,
		.flags_fragment = 0,
		.ttl = 0x40,
		.protocol = IPV4_PROT_UDP,
		.checksum = 0, /* fill this in later */
		.source = htonl(ip_aton("10.0.2.15")),
		.destination = htonl(ip_aton("10.0.2.3")),
	};

	uint16_t checksum = calculate_ipv4_checksum(&ipv4_out);
	ipv4_out.checksum = htons(checksum);

	memcpy(&buffer[offset], &ipv4_out, sizeof(struct ipv4_packet));
	offset += sizeof(struct ipv4_packet);

	uint16_t _udp_source = htons(50053); /* Use an ephemeral port */
	uint16_t _udp_destination = htons(53);
	uint16_t _udp_length = htons(sizeof(struct udp_packet) + payload_size);

	/* Now let's build a UDP packet */
	struct udp_packet udp_out = {
		.source_port = _udp_source,
		.destination_port = _udp_destination,
		.length = _udp_length,
		.checksum = 0,
	};

	/* XXX calculate checksum here */

	memcpy(&buffer[offset], &udp_out, sizeof(struct udp_packet));
	offset += sizeof(struct udp_packet);

	/* DNS header */
	struct dns_packet dns_out = {
		.qid = htons(0),
		.flags = htons(0x0100), /* Standard query */
		.questions = htons(1), /* 1 question */
		.answers = htons(0),
		.authorities = htons(0),
		.additional = htons(0),
	};

	memcpy(&buffer[offset], &dns_out, sizeof(struct dns_packet));
	offset += sizeof(struct dns_packet);

	memcpy(&buffer[offset], queries, queries_len);
	offset += queries_len;

	return offset;
}

static size_t print_dns_name(fs_node_t * tty, struct dns_packet * dns, size_t offset) {
	uint8_t * bytes = (uint8_t *)dns;
	while (1) {
		uint8_t c = bytes[offset];
		if (c == 0) {
			offset++;
			return offset;
		} else if (c >= 0xC0) {
			uint16_t ref = ((c - 0xC0) << 8) + bytes[offset+1];
			print_dns_name(tty, dns, ref);
			offset++;
			offset++;
			return offset;
		} else {
			for (int i = 0; i < c; ++i) {
				fprintf(tty,"%c",bytes[offset+1+i]);
			}
			fprintf(tty,".");
			offset += c + 1;
		}
	}

}

static void parse_dns_response(fs_node_t * tty, void * last_packet) {
	struct ethernet_packet * eth = (struct ethernet_packet *)last_packet;
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

	struct udp_packet * udp = (struct udp_packet *)ipv4->payload;
	uint16_t src_port = ntohs(udp->source_port);
	uint16_t dst_port = ntohs(udp->destination_port);
	uint16_t udp_len  = ntohs(udp->length);

	fprintf(tty, "UDP [%d → %d] length=%d bytes\n",
			src_port, dst_port, udp_len);

	struct dns_packet * dns = (struct dns_packet *)udp->payload;
	uint16_t dns_questions = ntohs(dns->questions);
	uint16_t dns_answers   = ntohs(dns->answers);
	fprintf(tty, "DNS - %d queries, %d answers\n",
			dns_questions, dns_answers);

	fprintf(tty, "Queries:\n");
	int offset = sizeof(struct dns_packet);
	int queries = 0;
	uint8_t * bytes = (uint8_t *)dns;
	while (queries < dns_questions) {
		offset = print_dns_name(tty, dns, offset);
		uint16_t * d = (uint16_t *)&bytes[offset];
		fprintf(tty, " - Type: %4x %4x\n", ntohs(d[0]), ntohs(d[1]));
		offset += 4;
		queries++;
	}

	fprintf(tty, "Answers:\n");
	int answers = 0;
	while (answers < dns_answers) {
		offset = print_dns_name(tty, dns, offset);
		uint16_t * d = (uint16_t *)&bytes[offset];
		fprintf(tty, " - Type: %4x %4x; ", ntohs(d[0]), ntohs(d[1]));
		offset += 4;
		uint32_t * t = (uint32_t *)&bytes[offset];
		fprintf(tty, "TTL: %d; ", ntohl(t[0]));
		offset += 4;
		uint16_t * l = (uint16_t *)&bytes[offset];
		int _l = ntohs(l[0]);
		fprintf(tty, "len: %d; ", _l);
		offset += 2;
		if (_l == 4) {
			uint32_t * i = (uint32_t *)&bytes[offset];
			char ip[16];
			ip_ntoa(ntohl(i[0]), ip);
			fprintf(tty, " Address: %s\n", ip);
		} else {
			if (ntohs(d[0]) == 5) {
				fprintf(tty, "CNAME: ");
				print_dns_name(tty, dns, offset);
				fprintf(tty, "\n");
			} else {
				fprintf(tty, "dunno\n");
			}
		}
		offset += _l;
		answers++;
	}
}

static void rtl_irq_handler(struct regs *r) {
	uint16_t status = inports(rtl_iobase + RTL_PORT_ISR);
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
				last_packet = buf_8;
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
}

static volatile uint8_t _lock;
static int next_tx_buf(void) {
	int out;
	spin_lock(&_lock);
	out = next_tx;
	next_tx++;
	if (next_tx == 4) {
		next_tx = 0;
	}
	spin_unlock(&_lock);
	return out;
}

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

	{
		fprintf(tty, "Sending TCP syn\n");
		int my_tx = next_tx_buf();
		uint8_t payload[] = { 0 };
		size_t packet_size = write_tcp_packet(rtl_tx_buffer[my_tx], payload, 0, (TCP_FLAGS_SYN | DATA_OFFSET_5));

		outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * my_tx, rtl_tx_phys[my_tx]);
		outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * my_tx, packet_size);
	}

	sleep_on(rx_wait);

	{
		struct ethernet_packet * eth = (struct ethernet_packet *)last_packet;
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

		ack_no = ntohl(tcp->seq_number) + 1;
		seq_no = ntohl(tcp->ack_number);
	}
	{
		fprintf(tty, "Sending TCP ack\n");
		int my_tx = next_tx_buf();
		uint8_t payload[] = { 0 };
		size_t packet_size = write_tcp_packet(rtl_tx_buffer[my_tx], payload, 0, (TCP_FLAGS_ACK | DATA_OFFSET_5));

		outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * my_tx, rtl_tx_phys[my_tx]);
		outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * my_tx, packet_size);
	}


	while (1) {

		sleep_on(rx_wait);

		{
			struct ethernet_packet * eth = (struct ethernet_packet *)last_packet;
#if 0
			uint16_t eth_type = ntohs(eth->type);

			fprintf(tty, "Ethernet II, Src: (%2x:%2x:%2x:%2x:%2x:%2x), Dst: (%2x:%2x:%2x:%2x:%2x:%2x) [type=%4x)\n",
					eth->source[0], eth->source[1], eth->source[2],
					eth->source[3], eth->source[4], eth->source[5],
					eth->destination[0], eth->destination[1], eth->destination[2],
					eth->destination[3], eth->destination[4], eth->destination[5],
					eth_type);
#endif


			struct ipv4_packet * ipv4 = (struct ipv4_packet *)eth->payload;
#if 0
			uint32_t src_addr = ntohl(ipv4->source);
			uint32_t dst_addr = ntohl(ipv4->destination);
			uint16_t length   = ntohs(ipv4->length);

			char src_ip[16];
			char dst_ip[16];

			ip_ntoa(src_addr, src_ip);
			ip_ntoa(dst_addr, dst_ip);

			fprintf(tty, "IP packet [%s → %s] length=%d bytes\n",
					src_ip, dst_ip, length);
#endif

			struct tcp_header * tcp = (struct tcp_header *)ipv4->payload;

			uint32_t l__ = ntohs(ipv4->length) - sizeof(struct tcp_header) - sizeof(struct ipv4_packet);

			seq_no = ntohl(tcp->ack_number);
			ack_no = ntohl(tcp->seq_number) + l__;

			write_fs(tty, 0, ntohs(ipv4->length)-sizeof(struct ipv4_packet)-sizeof(struct tcp_header), tcp->payload);
		}

		{
			int my_tx = next_tx_buf();
			uint8_t payload[] = { 0 };
			size_t packet_size = write_tcp_packet(rtl_tx_buffer[my_tx], payload, 0, (TCP_FLAGS_ACK | DATA_OFFSET_5));

			outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * my_tx, rtl_tx_phys[my_tx]);
			outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * my_tx, packet_size);
		}

	}
}

DEFINE_SHELL_FUNCTION(irc_test, "irc test") {
	char * payloads[] = {
		"NICK toarutest\r\nUSER toaru 0 * :Toaru Test\r\nJOIN #levchins\r\n\0\0\0",
		"PRIVMSG #levchins :99 bottles of beer on the wall\r\n\0\0",
		"PRIVMSG #levchins :99 bottles of beer\r\n\0\0",
		"PRIVMSG #levchins :Take one down\r\n\0\0",
		"PRIVMSG #levchins :pass it around\r\n\0\0",
		"PRIVMSG #levchins :98 bottles of beer on the wall\r\n\0\0",
		"PART #levchins :Thank you, and good night!\r\n\0\0",
		"QUIT\r\n\0\0",
	};
	for (unsigned int i = 0; i < sizeof(payloads) / sizeof(uint8_t *); ++i) {
		int my_tx = next_tx_buf();
		int l = strlen(payloads[i]);
		size_t packet_size = write_tcp_packet(rtl_tx_buffer[my_tx], (uint8_t *)payloads[i], l, (TCP_FLAGS_ACK | DATA_OFFSET_5));

		outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * my_tx, rtl_tx_phys[my_tx]);
		outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * my_tx, packet_size);

		unsigned long s, ss;
		relative_time(0, 500, &s, &ss);
		sleep_until((process_t *)current_process, s, ss);
		switch_task(0);
	}

	return 0;
}

static char irc_payload[512];

static void irc_send(char * payload) {
	int my_tx = next_tx_buf();
	int l = strlen(payload);
	size_t packet_size = write_tcp_packet(rtl_tx_buffer[my_tx], (uint8_t *)payload, l, (TCP_FLAGS_ACK | DATA_OFFSET_5));

	outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * my_tx, rtl_tx_phys[my_tx]);
	outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * my_tx, packet_size);
}

DEFINE_SHELL_FUNCTION(irc_init, "irc connector") {
	if (argc < 2) {
		fprintf(tty, "Specify a username\n");
		return 1;
	}

	char * nick = argv[1];

	sprintf(irc_payload, "NICK %s\r\nUSER %s * 0 :%s\r\n", nick, nick, nick);
	irc_send(irc_payload);

	return 0;
}

DEFINE_SHELL_FUNCTION(irc_join, "irc channel tool") {

	if (argc < 2) {
		fprintf(tty, "Specify a channel.\n");
		return 1;
	}

	char * channel = argv[1];

	sprintf(irc_payload, "JOIN %s\r\n", channel);
	irc_send(irc_payload);

	while (1) {
		fprintf(tty, "%s> ", channel);
		char input[400];
		int c = debug_shell_readline(tty, input, 400);
		input[c] = '\0';

		if (!strcmp(input, "/part")) {
			sprintf(irc_payload, "PART %s\r\n", channel);
			irc_send(irc_payload);
			break;
		}

		sprintf(irc_payload, "PRIVMSG %s :%s\r\n", channel, input);
		irc_send(irc_payload);
	}

	return 0;
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
		for (int i = 0; i < 6; ++i) {
			mac[i] = inports(rtl_iobase + RTL_PORT_MAC + i);
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

		{
			fprintf(tty, "Sending DHCP discover\n");
			size_t packet_size = write_dhcp_packet(rtl_tx_buffer[next_tx]);

			outportl(rtl_iobase + RTL_PORT_TXBUF + 4 * next_tx, rtl_tx_phys[next_tx]);
			outportl(rtl_iobase + RTL_PORT_TXSTAT + 4 * next_tx, packet_size);

			next_tx++;
			if (next_tx == 4) {
				next_tx = 0;
			}
		}

		sleep_on(rx_wait);

		{
			struct ethernet_packet * eth = (struct ethernet_packet *)last_packet;
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

			struct udp_packet * udp = (struct udp_packet *)ipv4->payload;;
			uint16_t src_port = ntohs(udp->source_port);
			uint16_t dst_port = ntohs(udp->destination_port);
			uint16_t udp_len  = ntohs(udp->length);

			fprintf(tty, "UDP [%d → %d] length=%d bytes\n",
					src_port, dst_port, udp_len);

			struct dhcp_packet * dhcp = (struct dhcp_packet *)udp->payload;
			uint32_t yiaddr = ntohl(dhcp->yiaddr);

			char yiaddr_ip[16];
			ip_ntoa(yiaddr, yiaddr_ip);
			fprintf(tty,  "DHCP Offer: %s\n", yiaddr_ip);
		}

		fprintf(tty, "Card is configured, going to start worker thread now.\n");

		create_kernel_tasklet(rtl_netd, "[netd]", tty);

	} else {
		return -1;
	}
	return 0;
}



static int init(void) {
	BIND_SHELL_FUNCTION(rtl);
	BIND_SHELL_FUNCTION(irc_test);
	BIND_SHELL_FUNCTION(irc_init);
	BIND_SHELL_FUNCTION(irc_join);
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
