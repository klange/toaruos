/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */
#include <kernel/module.h>
#include <kernel/logging.h>
#include <kernel/ipv4.h>
#include <kernel/printf.h>
#include <kernel/tokenize.h>
#include <kernel/mod/net.h>
#include <kernel/mod/procfs.h>

#include <toaru/list.h>
#include <toaru/hashmap.h>

static hashmap_t * dns_cache;
static list_t * dns_waiters = NULL;
static uint32_t _dns_server;

static hashmap_t *_tcp_sockets = NULL;
static hashmap_t *_udp_sockets = NULL;

static void parse_dns_response(fs_node_t * tty, void * last_packet);
static size_t write_dns_packet(uint8_t * buffer, size_t queries_len, uint8_t * queries);
size_t write_dhcp_request(uint8_t * buffer, uint8_t * ip);
static size_t write_arp_request(uint8_t * buffer, uint32_t ip);

static uint8_t _gateway[6] = {255,255,255,255,255,255};

static struct netif _netif = {0};

static int tasklet_pid = 0;

uint32_t get_primary_dns(void);

static uint32_t netif_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char * buf = malloc(4096);

	struct netif * netif = &_netif;
	char ip[16];
	ip_ntoa(netif->source, ip);
	char dns[16];
	ip_ntoa(get_primary_dns(), dns);
	char gw[16];
	ip_ntoa(netif->gateway, gw);

	if (netif->hwaddr[0] == 0 &&
		netif->hwaddr[1] == 0 &&
		netif->hwaddr[2] == 0 &&
		netif->hwaddr[3] == 0 &&
		netif->hwaddr[4] == 0 &&
		netif->hwaddr[5] == 0) {

		sprintf(buf, "no network\n");
	} else {
		sprintf(buf,
			"ip:\t%s\n"
			"mac:\t%2x:%2x:%2x:%2x:%2x:%2x\n"
			"device:\t%s\n"
			"dns:\t%s\n"
			"gateway:\t%s\n"
			,
			ip,
			netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2], netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5],
			netif->driver,
			dns,
			gw
		);
	}

	size_t _bsize = strlen(buf);
	if (offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

static struct procfs_entry netif_entry = {
	0, /* filled by install */
	"netif",
	netif_func,
};

void init_netif_funcs(get_mac_func mac_func, get_packet_func get_func, send_packet_func send_func, char * device) {
	_netif.get_mac = mac_func;
	_netif.get_packet = get_func;
	_netif.send_packet = send_func;
	_netif.driver = device;
	memcpy(_netif.hwaddr, _netif.get_mac(), sizeof(_netif.hwaddr));

	if (!netif_entry.id) {
		int (*procfs_install)(struct procfs_entry *) = (int (*)(struct procfs_entry *))(uintptr_t)hashmap_get(modules_get_symbols(),"procfs_install");
		if (procfs_install) {
			procfs_install(&netif_entry);
		}
	}

	if (!tasklet_pid) {
		tasklet_pid = create_kernel_tasklet(net_handler, "[net]", NULL);
		debug_print(NOTICE, "Network worker tasklet started with pid %d", tasklet_pid);
	}
}

struct netif * get_default_network_interface(void) {
	return &_netif;
}

uint32_t get_primary_dns(void) {
	return _dns_server;
}

uint32_t ip_aton(const char * in) {
	char ip[16];
	char * c = ip;
	uint32_t out[4];
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

size_t dns_name_to_normal_name(struct dns_packet * dns, size_t offset, char * buf) {
	uint8_t * bytes = (uint8_t *)dns;
	size_t i = 0;

	while (1) {
		uint8_t c = bytes[offset];
		if (c == 0) break;
		if (c >= 0xC0) {
			uint16_t ref = ((c - 0xC0) << 8) + bytes[offset+1];
			i += dns_name_to_normal_name(dns, ref, &buf[i]);
			return i;
		}
		offset++;

		for (size_t j = 0; j < c; j++) {
			buf[i] = bytes[offset];
			i++;
			offset++;
		}
		buf[i] = '.';
		i++;
		buf[i] = '\0';
	}
	if (i == 0) return 0;

	buf[i-1] = '\0';
	return i-1;
}

size_t get_dns_name(char * buffer, struct dns_packet * dns, size_t offset) {
	uint8_t * bytes = (uint8_t *)dns;
	while (1) {
		uint8_t c = bytes[offset];
		if (c == 0) {
			offset++;
			return offset;
		} else if (c >= 0xC0) {
			uint16_t ref = ((c - 0xC0) << 8) + bytes[offset+1];
			get_dns_name(buffer, dns, ref);
			offset++;
			offset++;
			return offset;
		} else {
			for (int i = 0; i < c; ++i) {
				*buffer = bytes[offset+1+i];
				buffer++;
				*buffer = '\0';
			}
			*buffer = '.';
			buffer++;
			*buffer = '\0';
			offset += c + 1;
		}
	}
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

static void socket_alert_waiters(struct socket * sock) {
	if (sock->alert_waiters) {
		while (sock->alert_waiters->head) {
			node_t * node = list_dequeue(sock->alert_waiters);
			process_t * p = node->value;
			process_alert_node(p, sock);
			free(node);
		}
	}
}


static int socket_check(fs_node_t * node) {
	struct socket * sock = node->device;

	if (sock->bytes_available) {
		return 0;
	}

	if (sock->packet_queue->length > 0) {
		return 0;
	}

	return 1;
}

static int socket_wait(fs_node_t * node, void * process) {
	struct socket * sock = node->device;

	if (!list_find(sock->alert_waiters, process)) {
		list_insert(sock->alert_waiters, process);
	}

	list_insert(((process_t *)process)->node_waits, sock);
	return 0;
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

static int gethost(char * name, uint32_t * ip) {
	if (is_ip(name)) {
		debug_print(WARNING, "   IP: %x", ip_aton(name));
		*ip = ip_aton(name);
		return 0;
	} else {
		if (hashmap_has(dns_cache, name)) {
			*ip = ip_aton(hashmap_get(dns_cache, name));
			debug_print(WARNING, "   In Cache: %s → %x", name, ip);
			return 0;
		} else {
			debug_print(WARNING, "   Not in cache: %s", name);
			debug_print(WARNING, "   Still needs look up.");
			char * xname = strdup(name);
			char * queries = malloc(1024);
			queries[0] = '\0';
			char * subs[10]; /* 10 is probably not the best number. */
			int argc = tokenize(xname, ".", subs);
			int n = 0;
			for (int i = 0; i < argc; ++i) {
				debug_print(WARNING, "dns [%d]%s", strlen(subs[i]), subs[i]);
				sprintf(&queries[n], "%c%s", strlen(subs[i]), subs[i]);
				n += strlen(&queries[n]);
			}
			int c = strlen(queries) + 1;
			queries[c+0] = 0x00;
			queries[c+1] = 0x01; /* A */
			queries[c+2] = 0x00;
			queries[c+3] = 0x01; /* IN */
			free(xname);

			debug_print(WARNING, "Querying...");

			void * tmp = malloc(1024);
			size_t packet_size = write_dns_packet(tmp, c + 4, (uint8_t *)queries);
			free(queries);

			_netif.send_packet(tmp, packet_size);
			free(tmp);

			/* wait for response */
			if (current_process->id != tasklet_pid) {
				sleep_on(dns_waiters);
			}
			if (hashmap_has(dns_cache, name)) {
				*ip = ip_aton(hashmap_get(dns_cache, name));
				debug_print(WARNING, "   Now in cache: %s → %x", name, ip);
				return 0;
			} else {
				if (current_process->id == tasklet_pid) {
					debug_print(WARNING, "Query hasn't returned yet, but we're in the network thread, so we need to yield.");
					return 2;
				}
				gethost(name,ip);
				return 1;
			}
		}
	}
}

static int net_send_tcp(struct socket *socket, uint16_t flags, uint8_t * payload, uint32_t payload_size);

static void socket_close(fs_node_t * node) {
	debug_print(ERROR, "Closing socket");
	struct socket * sock = node->device;
	if (sock->status == 1) return; /* already closed */
	net_send_tcp(sock, TCP_FLAGS_ACK | TCP_FLAGS_FIN, NULL, 0);
	sock->status = 2;
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
	if (gethost(name, &ip)) return NULL;

	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, name);
	fnode->mask = 0666;
	fnode->flags   = FS_CHARDEVICE;
	fnode->read    = socket_read;
	fnode->write   = socket_write;
	fnode->close   = socket_close;
	fnode->device  = (void *)net_open(SOCK_STREAM);
	fnode->selectcheck = socket_check;
	fnode->selectwait = socket_wait;

	net_connect((struct socket *)fnode->device, ip, port);

	return fnode;
}

static int ioctl_netfs(fs_node_t * node, int request, void * argp) {
	switch (request) {
		case 0x5000: {
			/* */
			debug_print(INFO, "DNS query from userspace");
			void ** x = (void **)argp;
			char * host = x[0];
			uint32_t * ip = x[1];
			/* TODO: Validate */
			return gethost(host, ip);
		}
	}
	return 0;
}

static size_t write_dns_packet(uint8_t * buffer, size_t queries_len, uint8_t * queries) {
	size_t offset = 0;
	size_t payload_size = sizeof(struct dns_packet) + queries_len;

	/* Then, let's write an ethernet frame */
	struct ethernet_packet eth_out = {
		.source = { _netif.hwaddr[0], _netif.hwaddr[1], _netif.hwaddr[2],
		            _netif.hwaddr[3], _netif.hwaddr[4], _netif.hwaddr[5] },
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
		.source = htonl(_netif.source),
		.destination = htonl(_dns_server),
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
	//memset(eth->destination, 0xFF, sizeof(eth->destination));
	memcpy(eth->destination, _gateway, sizeof(_gateway));
	eth->type = htons(ether_type);

	if (payload_size) {
		memcpy(eth->payload, payload, payload_size);
	}

	netif->send_packet((uint8_t*)eth, sizeof(struct ethernet_packet) + payload_size);

	free(eth);

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
	ipv4->source = htonl(_netif.source),
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
		free(payload);
	}

	// TODO: netif should not be a global thing. But the route should be looked up here and a netif object created/returned
	int out = net_send_ether(socket, &_netif, ETHERNET_TYPE_IPV4, ipv4, sizeof(struct ipv4_packet) + payload_size);
	free(ipv4);
	return out;
}

static int net_send_tcp(struct socket *socket, uint16_t flags, uint8_t * payload, uint32_t payload_size) {
	struct tcp_header *tcp = malloc(sizeof(struct tcp_header) + payload_size);

	tcp->source_port = htons(socket->port_recv);
	tcp->destination_port = htons(socket->port_dest);
	tcp->seq_number = htonl(socket->proto_sock.tcp_socket.seq_no);
	tcp->ack_number = flags & (TCP_FLAGS_ACK) ? htonl(socket->proto_sock.tcp_socket.ack_no) : 0;
	tcp->flags = htons(0x5000 ^ (flags & 0xFF));
	tcp->window_size = htons(1548-54);
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
	socket_alert_waiters(socket);
	return 1;
}

int net_send(struct socket* socket, uint8_t* payload, size_t payload_size, int flags) {
	return net_send_tcp(socket, TCP_FLAGS_ACK | TCP_FLAGS_PSH, payload, payload_size);
}

size_t net_recv(struct socket* socket, uint8_t* buffer, size_t len) {
	tcpdata_t *tcpdata = NULL;
	node_t *node = NULL;

	debug_print(INFO, "0x%x [socket]", socket);

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

	size_to_read = MIN(len, socket->bytes_available);

	if (tcpdata->payload != 0) {
		memcpy(buffer + offset, tcpdata->payload + socket->bytes_read, size_to_read);
	}

	offset += size_to_read;

	if (size_to_read < socket->bytes_available) {
		socket->bytes_available -= size_to_read;
		socket->bytes_read += size_to_read;
		socket->current_packet = tcpdata;
	} else {
		socket->bytes_available = 0;
		socket->current_packet = NULL;
		free(tcpdata->payload);
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

		if (socket->status == 2) {
			debug_print(WARNING, "Received packet while connection is in 'closing' statuus");
		}

		if (socket->status == 1) {
			if ((htons(tcp->flags) & TCP_FLAGS_FIN)) {
				debug_print(WARNING, "TCP close sequence continues");
				return;
			}
			if ((htons(tcp->flags) & TCP_FLAGS_ACK)) {
				debug_print(WARNING, "TCP close sequence continues");
				return;
			}
			debug_print(ERROR, "Socket is closed? Should send FIN. socket=0x%x flags=0x%x", socket, tcp->flags);
			net_send_tcp(socket, TCP_FLAGS_FIN | TCP_FLAGS_ACK, NULL, 0);
			return;
		}

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
				free(tcpdata);
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
			socket_alert_waiters(socket);

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
	debug_print(WARNING, "UDP response!");

	/* Short-circuit DNS */
	if (ntohs(udp->source_port) == 53) {
		debug_print(WARNING, "UDP response to DNS query!");
		parse_dns_response(debug_file, udp);
		return;
	}

	if (ntohs(udp->source_port) == 67) {
		debug_print(WARNING, "UDP response to DHCP!");

		{
			void * tmp = malloc(1024);
			size_t packet_size = write_arp_request(tmp, _netif.gateway);
			_netif.send_packet(tmp, packet_size);
			free(tmp);
		}

		return;
	}

	/* Find socket */
	if (hashmap_has(_udp_sockets, (void *)ntohs(udp->source_port))) {
		/* Do the thing */

	} else {
		/* ??? */
	}

}

static void net_handle_ipv4(struct ipv4_packet * ipv4) {
	debug_print(INFO, "net_handle_ipv4: ENTER");
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
	socket->alert_waiters = list_create();

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

static void placeholder_dhcp(void) {
	debug_print(NOTICE, "Sending DHCP discover");
	void * tmp = malloc(1024);
	size_t packet_size = write_dhcp_packet(tmp);
	_netif.send_packet(tmp, packet_size);
	free(tmp);

	while (1) {
		struct ethernet_packet * eth = (struct ethernet_packet *)_netif.get_packet();
		uint16_t eth_type = ntohs(eth->type);

		debug_print(NOTICE, "Ethernet II, Src: (%2x:%2x:%2x:%2x:%2x:%2x), Dst: (%2x:%2x:%2x:%2x:%2x:%2x) [type=%4x])",
				eth->source[0], eth->source[1], eth->source[2],
				eth->source[3], eth->source[4], eth->source[5],
				eth->destination[0], eth->destination[1], eth->destination[2],
				eth->destination[3], eth->destination[4], eth->destination[5],
				eth_type);

		if (eth_type != 0x0800) {
			debug_print(WARNING, "ARP packet while waiting for DHCP...");
			free(eth);
			continue;
		}


		struct ipv4_packet * ipv4 = (struct ipv4_packet *)eth->payload;
		uint32_t src_addr = ntohl(ipv4->source);
		uint32_t dst_addr = ntohl(ipv4->destination);
		uint16_t length   = ntohs(ipv4->length);

		char src_ip[16];
		char dst_ip[16];

		ip_ntoa(src_addr, src_ip);
		ip_ntoa(dst_addr, dst_ip);

		debug_print(NOTICE, "IP packet [%s → %s] length=%d bytes",
				src_ip, dst_ip, length);

		if (ipv4->protocol != IPV4_PROT_UDP) {
			debug_print(WARNING, "Protocol: %d", ipv4->protocol);
			debug_print(WARNING, "Bad packet...");
			free(eth);
			continue;
		}

		struct udp_packet * udp = (struct udp_packet *)ipv4->payload;;
		uint16_t src_port = ntohs(udp->source_port);
		uint16_t dst_port = ntohs(udp->destination_port);
		uint16_t udp_len  = ntohs(udp->length);

		debug_print(NOTICE, "UDP [%d → %d] length=%d bytes",
				src_port, dst_port, udp_len);

		if (dst_port != 68) {
			debug_print(WARNING, "Destination port: %d", dst_port);
			debug_print(WARNING, "Bad packet...");
			free(eth);
			continue;
		}

		struct dhcp_packet * dhcp = (struct dhcp_packet *)udp->payload;
		uint32_t yiaddr = ntohl(dhcp->yiaddr);

		char yiaddr_ip[16];
		ip_ntoa(yiaddr, yiaddr_ip);
		debug_print(NOTICE,  "DHCP Offer: %s", yiaddr_ip);

		_netif.source = yiaddr;

		debug_print(NOTICE,"  Scanning offer for DNS servers...");

		size_t i = sizeof(struct dhcp_packet);
		size_t j = 0;
		while (i < length) {
			uint8_t type = dhcp->options[j];
			uint8_t len  = dhcp->options[j+1];
			uint8_t * data = &dhcp->options[j+2];

			debug_print(NOTICE,"    type=%d, len=%d", type, len);
			if (type == 255) {
				break;
			} else if (type == 6) {
				/* DNS Server! */
				uint32_t dnsaddr = ntohl(*(uint32_t *)data);
				char ip[16];
				ip_ntoa(dnsaddr, ip);
				debug_print(NOTICE, "Found one: %s", ip);
				_dns_server = dnsaddr;
			} else if (type == 3) {
				_netif.gateway = ntohl(*(uint32_t *)data);
			}

			j += 2 + len;
			i += 2 + len;
		}

		debug_print(NOTICE, "Sending DHCP Request...");
		void * tmp = malloc(1024);
		size_t packet_size = write_dhcp_request(tmp, (uint8_t *)&dhcp->yiaddr);
		_netif.send_packet(tmp, packet_size);
		free(tmp);

		free(eth);

		break;
	}

}

struct arp {
	uint16_t htype;
	uint16_t proto;

	uint8_t hlen;
	uint8_t plen;

	uint16_t oper;

	uint8_t sender_ha[6];
	uint32_t sender_ip;
	uint8_t target_ha[6];
	uint32_t target_ip;

	uint8_t padding[18];
} __attribute__((packed));

static size_t write_arp_response(uint8_t * buffer, struct arp * source) {
	size_t offset = 0;

	/* Then, let's write an ethernet frame */
	struct ethernet_packet eth_out = {
		.source = { _netif.hwaddr[0], _netif.hwaddr[1], _netif.hwaddr[2],
		            _netif.hwaddr[3], _netif.hwaddr[4], _netif.hwaddr[5] },
		.destination = BROADCAST_MAC,
		.type = htons(0x0806),
	};

	memcpy(&buffer[offset], &eth_out, sizeof(struct ethernet_packet));
	offset += sizeof(struct ethernet_packet);

	struct arp arp_out;

	arp_out.htype = source->htype;
	arp_out.proto = source->proto;

	arp_out.hlen = 6;
	arp_out.plen = 4;
	arp_out.oper = ntohs(2);

	arp_out.sender_ha[0] = _netif.hwaddr[0];
	arp_out.sender_ha[1] = _netif.hwaddr[1];
	arp_out.sender_ha[2] = _netif.hwaddr[2];
	arp_out.sender_ha[3] = _netif.hwaddr[3];
	arp_out.sender_ha[4] = _netif.hwaddr[4];
	arp_out.sender_ha[5] = _netif.hwaddr[5];
	arp_out.sender_ip = ntohl(_netif.source);

	arp_out.target_ha[0] = source->sender_ha[0];
	arp_out.target_ha[1] = source->sender_ha[1];
	arp_out.target_ha[2] = source->sender_ha[2];
	arp_out.target_ha[3] = source->sender_ha[3];
	arp_out.target_ha[4] = source->sender_ha[4];
	arp_out.target_ha[5] = source->sender_ha[5];

	arp_out.target_ip = source->sender_ip;

	memcpy(&buffer[offset], &arp_out, sizeof(struct arp));
	offset += sizeof(struct arp);

	return offset;
}

static size_t write_arp_request(uint8_t * buffer, uint32_t ip) {
	size_t offset = 0;

	debug_print(WARNING, "Request ARP from gateway address %x", ip);

	/* Then, let's write an ethernet frame */
	struct ethernet_packet eth_out = {
		.source = { _netif.hwaddr[0], _netif.hwaddr[1], _netif.hwaddr[2],
		            _netif.hwaddr[3], _netif.hwaddr[4], _netif.hwaddr[5] },
		.destination = BROADCAST_MAC,
		.type = htons(0x0806),
	};

	memcpy(&buffer[offset], &eth_out, sizeof(struct ethernet_packet));
	offset += sizeof(struct ethernet_packet);

	struct arp arp_out;

	arp_out.htype = ntohs(1);

	debug_print(WARNING, "Request ARP from gateway address %x", ip);
	arp_out.proto = ntohs(0x0800);

	arp_out.hlen = 6;
	arp_out.plen = 4;
	arp_out.oper = ntohs(1);

	arp_out.sender_ha[0] = _netif.hwaddr[0];
	arp_out.sender_ha[1] = _netif.hwaddr[1];
	arp_out.sender_ha[2] = _netif.hwaddr[2];
	arp_out.sender_ha[3] = _netif.hwaddr[3];
	arp_out.sender_ha[4] = _netif.hwaddr[4];
	arp_out.sender_ha[5] = _netif.hwaddr[5];
	arp_out.sender_ip = ntohl(_netif.source);

	arp_out.target_ha[0] = 0;
	arp_out.target_ha[1] = 0;
	arp_out.target_ha[2] = 0;
	arp_out.target_ha[3] = 0;
	arp_out.target_ha[4] = 0;
	arp_out.target_ha[5] = 0;

	arp_out.target_ip = ntohl(ip);

	memcpy(&buffer[offset], &arp_out, sizeof(struct arp));
	offset += sizeof(struct arp);

	return offset;
}


static void net_handle_arp(struct ethernet_packet * eth) {
	debug_print(WARNING, "ARP packet...");

	struct arp * arp = (struct arp *)&eth->payload;

	char sender_ip[16];
	char target_ip[16];

	ip_ntoa(ntohl(arp->sender_ip), sender_ip);
	ip_ntoa(ntohl(arp->target_ip), target_ip);

	debug_print(WARNING, "%2x:%2x:%2x:%2x:%2x:%2x (%s) → %2x:%2x:%2x:%2x:%2x:%2x (%s) is",
		arp->sender_ha[0],
		arp->sender_ha[1],
		arp->sender_ha[2],
		arp->sender_ha[3],
		arp->sender_ha[4],
		arp->sender_ha[5],
		sender_ip,
		arp->target_ha[0],
		arp->target_ha[1],
		arp->target_ha[2],
		arp->target_ha[3],
		arp->target_ha[4],
		arp->target_ha[5],
		target_ip);

	if (ntohs(arp->oper) == 1) {

		if (ntohl(arp->target_ip) == _netif.source) {
			debug_print(WARNING, "That's us!");

			{
				void * tmp = malloc(1024);
				size_t packet_size = write_arp_response(tmp, arp);
				_netif.send_packet(tmp, packet_size);
				free(tmp);
			}

		}

	} else {
		if (ntohl(arp->target_ip) == _netif.source) {
			debug_print(WARNING, "It's a response to our query!");
			if (ntohl(arp->sender_ip) == _netif.gateway) {
				_gateway[0] = arp->sender_ha[0];
				_gateway[1] = arp->sender_ha[1];
				_gateway[2] = arp->sender_ha[2];
				_gateway[3] = arp->sender_ha[3];
				_gateway[4] = arp->sender_ha[4];
				_gateway[5] = arp->sender_ha[5];
			}
		} else {
			debug_print(WARNING, "Response to someone else...\n");
		}
	}

}

void net_handler(void * data, char * name) {
	/* Network Packet Handler*/
	_netif.extra = NULL;

	_dns_server = ip_aton("10.0.2.3");

	placeholder_dhcp();

	dns_waiters = list_create();

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
				net_handle_arp(eth);
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
		55,
		2,
		3,
		6,
		255, /* END */
	};

	payload_size += sizeof(dhcp_options);

	/* Then, let's write an ethernet frame */
	struct ethernet_packet eth_out = {
		.source = { _netif.hwaddr[0], _netif.hwaddr[1], _netif.hwaddr[2],
		            _netif.hwaddr[3], _netif.hwaddr[4], _netif.hwaddr[5] },
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

		.chaddr = { _netif.hwaddr[0], _netif.hwaddr[1], _netif.hwaddr[2],
		            _netif.hwaddr[3], _netif.hwaddr[4], _netif.hwaddr[5] },
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

size_t write_dhcp_request(uint8_t * buffer, uint8_t * ip) {
	size_t offset = 0;
	size_t payload_size = sizeof(struct dhcp_packet);

	/* First, let's figure out how big this is supposed to be... */

	uint8_t dhcp_options[] = {
		53, /* Message type */
		1,  /* Length: 1 */
		3,  /* Request */
		50,
		4,  /* requested ip */
		ip[0],ip[1],ip[2],ip[3],
		55,
		2,
		3,
		6,
		255, /* END */
	};

	payload_size += sizeof(dhcp_options);

	/* Then, let's write an ethernet frame */
	struct ethernet_packet eth_out = {
		.source = { _netif.hwaddr[0], _netif.hwaddr[1], _netif.hwaddr[2],
		            _netif.hwaddr[3], _netif.hwaddr[4], _netif.hwaddr[5] },
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

		.chaddr = { _netif.hwaddr[0], _netif.hwaddr[1], _netif.hwaddr[2],
		            _netif.hwaddr[3], _netif.hwaddr[4], _netif.hwaddr[5] },
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
	struct udp_packet * udp = (struct udp_packet *)last_packet;
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
		char buf[1024];
		size_t ret = dns_name_to_normal_name(dns, offset, buf);
		debug_print(WARNING, "%d - %s", ret, buf);
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
			debug_print(NOTICE, "Domain [%s] maps to [%s]", buf, ip);
			if (!hashmap_has(dns_cache, buf)) {
				hashmap_set(dns_cache, buf, strdup(ip));
			}
		} else {
			if (ntohs(d[0]) == 5) {
				fprintf(tty, "CNAME: ");
				char buffer[256];
				get_dns_name(buffer, dns, offset);
				fprintf(tty, "%s\n", buffer);
				if (strlen(buffer)) {
					buffer[strlen(buffer)-1] = '\0';
				}
				uint32_t addr;
				if (gethost(buffer,&addr) == 2) {
					debug_print(WARNING,"Can't provide a response yet, but going to query again in a moment.");
				} else {
					if (!hashmap_has(dns_cache, buf)) {
						char ip[16];
						ip_ntoa(addr, ip);
						hashmap_set(dns_cache, buf, strdup(ip));
						fprintf(tty, "resolves to %s\n", ip);
					}
				}
			} else {
				fprintf(tty, "dunno\n");
			}
		}
		offset += _l;
		answers++;
	}

	wakeup_queue(dns_waiters);
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
	fnode->ioctl   = ioctl_netfs;
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
