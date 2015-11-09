/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <module.h>
#include <logging.h>
#include <hashmap.h>
#include <ipv4.h>
#include <printf.h>
#include <mod/net.h>

static hashmap_t * dns_cache;

static uint8_t mac[6];

static hashmap_t *_tcp_sockets = NULL;
static hashmap_t *_udp_sockets = NULL;

static struct netif _netif;

void init_netif_funcs(get_mac_func mac_func, get_packet_func get_func, send_packet_func send_func) {
	_netif.get_mac = mac_func;
	_netif.get_packet = get_func;
	_netif.send_packet = send_func;
	memcpy(_netif.hwaddr, _netif.get_mac(), sizeof(_netif.hwaddr));
}

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

uint16_t calculate_tcp_checksum(struct tcp_check_header * p, struct tcp_header * h, void * d, size_t payload_size) {
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

	uint16_t d_words = payload_size / 2;

	s = (uint16_t *)d;
	for (unsigned int i = 0; i < d_words; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	if (d_words * 2 != payload_size) {
		uint8_t * t = (uint8_t *)d;
		uint8_t tmp[2];
		tmp[0] = t[d_words * sizeof(uint16_t)];
		tmp[1] = 0;

		uint16_t * f = (uint16_t *)tmp;

		sum += ntohs(f[0]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	return ~(sum & 0xFFFF) & 0xFFFF;
}

static struct dirent * readdir_netfs(fs_node_t *node, uint32_t index) {
	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, "..");
		return out;
	}

	index -= 2;
	return NULL;
}

size_t print_dns_name(fs_node_t * tty, struct dns_packet * dns, size_t offset) {
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

static int is_ip(char * name) {

	unsigned int dot_count = 0;
	unsigned int t = 0;

	for (char * c = name; *c != '\0'; ++c) {
		if ((*c < '0' || *c > '9') && (*c != '.')) return 0;
		if (*c == '.') {
			if (t > 255) return 0;
			dot_count++;
			t = 0;
		} else {
			t *= 10;
			t += *c - '0';
		}
		if (dot_count == 4) return 0;
	}
	if (dot_count != 3) return 0;

	return 1;
}

static char read_a_byte(struct socket * stream, int * status) {
	static char * foo = NULL;
	static char * read_ptr = NULL;
	static int have_bytes = 0;
	if (!foo) foo = malloc(4096);
	while (!have_bytes) {
		memset(foo, 0x00, 4096);
		have_bytes = net_recv(stream, (uint8_t *)foo, 4096);
		if (have_bytes == 0) {
			*status = 1;
			return 0;
		}
		debug_print(WARNING, "Received %d bytes...", have_bytes);
		read_ptr = foo;
	}

	char ret = *read_ptr;

	have_bytes -= 1;
	read_ptr++;

	return ret;
}


static char * fgets(char * buf, int size, struct socket * stream) {
	char * x = buf;
	int collected = 0;

	while (collected < size) {

		int status = 0;

		*x = read_a_byte(stream, &status);

		if (status == 1) {
			return buf;
		}

		collected++;

		if (*x == '\n') break;

		x++;
	}

	x++;
	*x = '\0';
	return buf;
}

static uint32_t socket_read(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t * buffer) {
	/* Sleep until we have something to receive */
#if 0
	fgets((char *)buffer, size, node->device);
	return strlen((char *)buffer);
#else
	return net_recv(node->device, buffer, size);
#endif
}
static uint32_t socket_write(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t * buffer) {
	/* Add the packet to the appropriate interface queue and send it off. */

	net_send((struct socket *)node->device, buffer, size, 0);
	return size;
}

uint16_t next_ephemeral_port(void) {
	static uint16_t next = 49152;

	if (next == 0) {
		assert(0 && "All out of ephemeral ports, halting this time.");
	}

	uint16_t out = next;
	next++;

	if (next == 0) {
		debug_print(WARNING, "Ran out of ephemeral ports - next time I'm going to bail.");
		debug_print(WARNING, "You really need to implement a bitmap here.");
	}

	return out;
}

fs_node_t * socket_ipv4_tcp_create(uint32_t dest, uint16_t target_port, uint16_t source_port) {

	/* Okay, first step is to get us added to the table so we can receive syns. */

	return NULL;

}


/* TODO: socket_close - TCP close; UDP... just clean us up */
/* TODO: socket_open - idk, whatever */

static fs_node_t * finddir_netfs(fs_node_t * node, char * name) {
	/* Should essentially find anything. */
	debug_print(WARNING, "Need to look up domain or check if is IP: %s", name);
	/* Block until lookup is complete */

	int port = 80;
	char * colon;
	if ((colon = strstr(name, ":"))) {
		/* Port numbers */
		*colon = '\0';
		colon++;
		port = atoi(colon);
	}

	uint32_t ip = 0;
	if (is_ip(name)) {
		debug_print(WARNING, "   IP: %x", ip_aton(name));
		ip = ip_aton(name);
	} else {
		if (hashmap_has(dns_cache, name)) {
			ip = ip_aton(hashmap_get(dns_cache, name));
			debug_print(WARNING, "   In Cache: %s → %x", name, ip);
		} else {
			debug_print(WARNING, "   Still needs look up.");
			return NULL;
		}
	}

	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, name);
	fnode->mask = 0555;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = socket_read;
	fnode->write   = socket_write;
	fnode->device  = (void *)net_open(SOCK_STREAM);

	net_connect((struct socket *)fnode->device, ip, port);

	return fnode;
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

static int net_send_ether(struct socket *socket, struct netif* netif, uint16_t ether_type, void* payload, uint32_t payload_size) {
	struct ethernet_packet *eth = malloc(sizeof(struct ethernet_packet) + payload_size);
	memcpy(eth->source, netif->hwaddr, sizeof(eth->source));
	memset(eth->destination, 0xFF, sizeof(eth->destination));
	eth->type = htons(ether_type);

	if (payload_size) {
		memcpy(eth->payload, payload, payload_size);
	}

	netif->send_packet((uint8_t*)eth, sizeof(struct ethernet_packet) + payload_size);

	return 1; // yolo
}

static int net_send_ip(struct socket *socket, int proto, void* payload, uint32_t payload_size) {
	struct ipv4_packet *ipv4 = malloc(sizeof(struct ipv4_packet) + payload_size);

	uint16_t _length = htons(sizeof(struct ipv4_packet) + payload_size);
	uint16_t _ident  = htons(1);

	ipv4->version_ihl = ((0x4 << 4) | (0x5 << 0)); /* 4 = ipv4, 5 = no options */
	ipv4->dscp_ecn = 0; /* not setting either of those */
	ipv4->length = _length;
	ipv4->ident = _ident;
	ipv4->flags_fragment = 0;
	ipv4->ttl = 0x40;
	ipv4->protocol = proto;
	ipv4->checksum = 0; // Fill in later */
	ipv4->source = htonl(ip_aton("10.0.2.15")),
	ipv4->destination = htonl(socket->ip);

	uint16_t checksum = calculate_ipv4_checksum(ipv4);
	ipv4->checksum = htons(checksum);

	if (proto == IPV4_PROT_TCP) {
		// Need to calculate TCP checksum
		struct tcp_check_header check_hd = {
			.source = ipv4->source,
			.destination = ipv4->destination,
			.zeros = 0,
			.protocol = 6,
			.tcp_len = htons(payload_size),
		};

		// debug_print(WARNING, "net_send_ip: Payload size: %d\n", payload_size);
		struct tcp_header* tcp_hdr = (struct tcp_header*)payload;
		// debug_print(WARNING, "net_send_ip: Header len htons: %d\n", TCP_HEADER_LENGTH_FLIPPED(tcp_hdr));
		size_t orig_payload_size = payload_size - TCP_HEADER_LENGTH_FLIPPED(tcp_hdr);

		uint16_t chk = calculate_tcp_checksum(&check_hd, tcp_hdr, tcp_hdr->payload, orig_payload_size);
		tcp_hdr->checksum = htons(chk);
	}

	if (payload) {
		memcpy(ipv4->payload, payload, payload_size);
	}

	// TODO: netif should not be a global thing. But the route should be looked up here and a netif object created/returned
	return net_send_ether(socket, &_netif, ETHERNET_TYPE_IPV4, ipv4, sizeof(struct ipv4_packet) + payload_size);
}

static int net_send_tcp(struct socket *socket, uint16_t flags, uint8_t * payload, uint32_t payload_size) {
	struct tcp_header *tcp = malloc(sizeof(struct tcp_header) + payload_size);

	tcp->source_port = htons(socket->port_recv);
	tcp->destination_port = htons(socket->port_dest);
	tcp->seq_number = htonl(socket->proto_sock.tcp_socket.seq_no);
	tcp->ack_number = flags & (TCP_FLAGS_ACK) ? htonl(socket->proto_sock.tcp_socket.ack_no) : 0;
	tcp->flags = htons(0x5000 ^ (flags & 0xFF));
	tcp->window_size = htons(1800);
	tcp->checksum = 0; // Fill in later
	tcp->urgent = 0;

	if ((flags & 0xff) == TCP_FLAGS_SYN) {
		// If only SYN set, expected ACK will be 1 despite no payload
		socket->proto_sock.tcp_socket.seq_no += 1;
	} else {
		socket->proto_sock.tcp_socket.seq_no += payload_size;
	}

	if (payload) {
		memcpy(tcp->payload, payload, payload_size);
	}

	return net_send_ip(socket, IPV4_PROT_TCP, tcp, sizeof(struct tcp_header) + payload_size);
}

struct socket* net_open(uint32_t type) {
	// This is a socket() call
	struct socket *sock = malloc(sizeof(struct socket));
	memset(sock, 0, sizeof(struct socket));
	sock->sock_type = type;

	return sock;
}

int net_close(struct socket* socket) {
	// socket->is_connected;
	socket->status = 1; /* Disconnected */
	wakeup_queue(socket->packet_wait);
	return 1;
}

int net_send(struct socket* socket, uint8_t* payload, size_t payload_size, int flags) {
	return net_send_tcp(socket, TCP_FLAGS_ACK | TCP_FLAGS_PSH, payload, payload_size);
}

size_t net_recv(struct socket* socket, uint8_t* buffer, size_t len) {
	tcpdata_t *tcpdata = NULL;
	node_t *node = NULL;

	debug_print(WARNING, "0x%x [socket]", socket);

	size_t offset = 0;
	size_t size_to_read = 0;

	do {

	if (socket->bytes_available) {
		tcpdata = socket->current_packet;
	} else {
		spin_lock(socket->packet_queue_lock);
		do {
			if (socket->packet_queue->length > 0) {
				node = list_dequeue(socket->packet_queue);
				spin_unlock(socket->packet_queue_lock);
				break;
			} else {
				if (socket->status == 1) {
					spin_unlock(socket->packet_queue_lock);
					debug_print(WARNING, "Socket closed, done reading.");
					return 0;
				}
				spin_unlock(socket->packet_queue_lock);
				sleep_on(socket->packet_wait);
				spin_lock(socket->packet_queue_lock);
			}
		} while (1);

		tcpdata = node->value;
		socket->bytes_available = tcpdata->payload_size;
		socket->bytes_read = 0;
		free(node);
	}

	size_to_read = MIN(len, offset + socket->bytes_available);

	if (tcpdata->payload != 0) {
		memcpy(buffer + offset, tcpdata->payload + socket->bytes_read, size_to_read);
	}

	offset += size_to_read;

	if (size_to_read < socket->bytes_available) {
		socket->bytes_available = socket->bytes_available - size_to_read;
		socket->bytes_read = size_to_read;
		socket->current_packet = tcpdata;
	} else {
		socket->bytes_available = 0;
		socket->current_packet = NULL;
		free(tcpdata);
	}


	} while (!size_to_read);


	return size_to_read;
}

static void net_handle_tcp(struct tcp_header * tcp, size_t length) {

	size_t data_length = length - TCP_HEADER_LENGTH_FLIPPED(tcp);

	/* Find socket */
	if (hashmap_has(_tcp_sockets, (void *)ntohs(tcp->destination_port))) {
		struct socket *socket = hashmap_get(_tcp_sockets, (void *)ntohs(tcp->destination_port));

		if (socket->proto_sock.tcp_socket.seq_no != ntohl(tcp->ack_number)) {
			// Drop packet
			debug_print(WARNING, "Dropping packet. Expected ack: %d | Got ack: %d",
					socket->proto_sock.tcp_socket.seq_no, ntohl(tcp->ack_number));
			return;
		}

		if ((htons(tcp->flags) & TCP_FLAGS_SYN) && (htons(tcp->flags) & TCP_FLAGS_ACK)) {
			socket->proto_sock.tcp_socket.ack_no = ntohl(tcp->seq_number) + data_length + 1;
			net_send_tcp(socket, TCP_FLAGS_ACK, NULL, 0);
			wakeup_queue(socket->proto_sock.tcp_socket.is_connected);
		} else if (htons(tcp->flags) & TCP_FLAGS_RES) {
			/* Reset doesn't necessarily mean close. */
			debug_print(WARNING, "net_handle_tcp: Received RST - socket closing");
			net_close(socket);
			return;
		} else {
			// Store a copy of the layer 5 data for a userspace recv() call
			tcpdata_t *tcpdata = malloc(sizeof(tcpdata_t));
			tcpdata->payload_size = length - TCP_HEADER_LENGTH_FLIPPED(tcp);

			if (tcpdata->payload_size == 0) {
				if (htons(tcp->flags) & TCP_FLAGS_FIN) {
					/* We should make sure we finish sending before closing. */
					debug_print(WARNING, "net_handle_tcp: Received FIN - socket closing with SYNACK");
					socket->proto_sock.tcp_socket.ack_no = ntohl(tcp->seq_number) + data_length + 1;
					net_send_tcp(socket, TCP_FLAGS_ACK | TCP_FLAGS_FIN, NULL, 0);
					wakeup_queue(socket->proto_sock.tcp_socket.is_connected);
					net_close(socket);
				}
				return;
			}

			// debug_print(WARNING, "net_handle_tcp: payload length: %d\n",  length);
			// debug_print(WARNING, "net_handle_tcp: flipped tcp flags hdr len: %d\n",  TCP_HEADER_LENGTH_FLIPPED(tcp));
			// debug_print(WARNING, "net_handle_tcp: tcpdata->payload_size: %d\n", tcpdata->payload_size);

			if (tcpdata->payload_size > 0) {
				tcpdata->payload = malloc(tcpdata->payload_size);
				memcpy(tcpdata->payload, tcp->payload, tcpdata->payload_size);
			} else {
				tcpdata->payload = NULL;
			}

			socket->proto_sock.tcp_socket.ack_no = ntohl(tcp->seq_number) + data_length;

			if ((htons(tcp->flags) & TCP_FLAGS_SYN) && (htons(tcp->flags) & TCP_FLAGS_ACK) && data_length == 0) {
				socket->proto_sock.tcp_socket.ack_no += 1;
			}

			socket->proto_sock.tcp_socket.ack_no = ntohl(tcp->seq_number) + tcpdata->payload_size;

			spin_lock(socket->packet_queue_lock);
			list_insert(socket->packet_queue, tcpdata);
			spin_unlock(socket->packet_queue_lock);

			// Send acknowledgement of receiving data
			net_send_tcp(socket, TCP_FLAGS_ACK, NULL, 0);

			wakeup_queue(socket->packet_wait);

			if (htons(tcp->flags) & TCP_FLAGS_FIN) {
				/* We should make sure we finish sending before closing. */
				debug_print(WARNING, "net_handle_tcp: Received FIN - socket closing with SYNACK");
				socket->proto_sock.tcp_socket.ack_no = ntohl(tcp->seq_number) + data_length + 1;
				net_send_tcp(socket, TCP_FLAGS_ACK | TCP_FLAGS_FIN, NULL, 0);
				wakeup_queue(socket->proto_sock.tcp_socket.is_connected);
				net_close(socket);
			}
		}
	} else {
		debug_print(WARNING, "net_handle_tcp: Received packet not associated with a socket!");
	}
}

static void net_handle_udp(struct udp_packet * udp, size_t length) {

	// size_t data_length = length - sizeof(struct tcp_header);

	/* Find socket */
	if (hashmap_has(_udp_sockets, (void *)ntohs(udp->source_port))) {
		/* Do the thing */

	} else {
		/* ??? */
	}

}

static void net_handle_ipv4(struct ipv4_packet * ipv4) {
	debug_print(WARNING, "net_handle_ipv4: ENTER");
	switch (ipv4->protocol) {
		case IPV4_PROT_TCP:
			net_handle_tcp((struct tcp_header *)ipv4->payload, ntohs(ipv4->length) - sizeof(struct ipv4_packet));
			break;
		case IPV4_PROT_UDP:
			net_handle_udp((struct udp_packet *)ipv4->payload, ntohs(ipv4->length) - sizeof(struct ipv4_packet));
			break;
		default:
			/* XXX */
			break;
	}
}

static struct ethernet_packet* net_receive(void) {
	struct ethernet_packet *eth = _netif.get_packet();

	return eth;
}

int net_connect(struct socket* socket, uint32_t dest_ip, uint16_t dest_port) {
	if (socket->sock_type == SOCK_DGRAM) {
		// Can't connect UDP
		return -1;
	}

	memset(socket->mac, 0, sizeof(socket->mac)); // idk
	socket->port_recv = next_ephemeral_port();
	socket->proto_sock.tcp_socket.is_connected = list_create();
	socket->proto_sock.tcp_socket.seq_no = 0;
	socket->proto_sock.tcp_socket.ack_no = 0;
	socket->proto_sock.tcp_socket.status = 0;

	socket->packet_queue = list_create();
	socket->packet_wait = list_create();

	socket->ip = dest_ip; //ip_aton("10.255.50.206");
	socket->port_dest = dest_port; //12345;

	debug_print(WARNING, "net_connect: using ephemeral port: %d", (void*)socket->port_recv);

	hashmap_set(_tcp_sockets, (void*)socket->port_recv, socket);

	net_send_tcp(socket, TCP_FLAGS_SYN, NULL, 0);
	// debug_print(WARNING, "net_connect:sent tcp SYN: %d", ret);

	// Race condition here - if net_handle_tcp runs and connects before this sleep
	sleep_on(socket->proto_sock.tcp_socket.is_connected);

	return 1;
}

void net_handler(void * data, char * name) {
	/* Network Packet Handler*/
	_netif.extra = NULL;

	_netif.source = 0x0a0a0a0a; // "10.10.10.10"

	_tcp_sockets = hashmap_create_int(0xFF);
	_udp_sockets = hashmap_create_int(0xFF);

	while (1) {
		struct ethernet_packet * eth = net_receive();

		if (!eth) continue;

		switch (ntohs(eth->type)) {
			case ETHERNET_TYPE_IPV4:
				net_handle_ipv4((struct ipv4_packet *)eth->payload);
				break;
			case ETHERNET_TYPE_ARP:
				// net_handle_arp(eth);
				break;
		}

		free(eth);
	}
}

size_t write_dhcp_packet(uint8_t * buffer) {
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
		.destination = htonl(ip_aton("255.255.255.255")),
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

static fs_node_t * netfs_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "net");
	fnode->mask = 0555;
	fnode->flags   = FS_DIRECTORY;
	fnode->readdir = readdir_netfs;
	fnode->finddir = finddir_netfs;
	fnode->nlink   = 1;
	return fnode;
}

static int init(void) {
	dns_cache = hashmap_create(10);

	hashmap_set(dns_cache, "dakko.us", strdup("104.131.140.26"));
	hashmap_set(dns_cache, "toaruos.org", strdup("104.131.140.26"));
	hashmap_set(dns_cache, "www.toaruos.org", strdup("104.131.140.26"));
	hashmap_set(dns_cache, "www.yelp.com", strdup("104.16.57.23"));
	hashmap_set(dns_cache, "s3-media2.fl.yelpcdn.com", strdup("199.27.79.175"));
	hashmap_set(dns_cache, "forum.osdev.org", strdup("173.255.206.39"));
	hashmap_set(dns_cache, "wolfgun.puckipedia.com", strdup("104.47.147.203"));
	hashmap_set(dns_cache, "irc.freenode.net", strdup("91.217.189.42"));
	hashmap_set(dns_cache, "i.imgur.com", strdup("23.235.47.193"));

	/* /dev/net/{domain|ip}/{protocol}/{port} */
	vfs_mount("/dev/net", netfs_create());

	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(net, init, fini);
