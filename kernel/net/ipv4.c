/**
 * @file  kernel/net/ipv4.c
 * @brief IPv4 protocol implementation.
 *
 * @copyright This file is part of ToaruOS and is released under the terms
 *            of the NCSA / University of Illinois License - see LICENSE.md
 * @author    2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/syscall.h>
#include <kernel/hashmap.h>
#include <kernel/vfs.h>

#include <kernel/net/netif.h>
#include <kernel/net/eth.h>

#include <sys/socket.h>

struct ipv4_packet {
	uint8_t  version_ihl;
	uint8_t  dscp_ecn;
	uint16_t length;
	uint16_t ident;
	uint16_t flags_fragment;
	uint8_t  ttl;
	uint8_t  protocol;
	uint16_t checksum;
	uint32_t source;
	uint32_t destination;
	uint8_t  payload[];
} __attribute__ ((packed)) __attribute__((aligned(2)));

struct icmp_header {
	uint8_t type;
	uint8_t code;
	uint16_t csum;
	uint16_t rest_of_header;
	uint8_t data[];
} __attribute__((packed)) __attribute__((aligned(2)));

struct udp_packet {
	uint16_t source_port;
	uint16_t destination_port;
	uint16_t length;
	uint16_t checksum;
	uint8_t  payload[];
} __attribute__ ((packed)) __attribute__((aligned(2)));

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
} __attribute__((packed)) __attribute__((aligned(2)));

struct tcp_check_header {
	uint32_t source;
	uint32_t destination;
	uint8_t  zeros;
	uint8_t  protocol;
	uint16_t tcp_len;
	uint8_t  tcp_header[];
};

#define IPV4_PROT_UDP 17
#define IPV4_PROT_TCP 6

static void ip_ntoa(const uint32_t src_addr, char * out) {
	snprintf(out, 16, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

static uint16_t icmp_checksum(struct ipv4_packet * packet) {
	uint32_t sum = 0;
	uint16_t * s = (uint16_t *)packet->payload;
	for (int i = 0; i < (ntohs(packet->length) - 20) / 2; ++i) {
		sum += ntohs(s[i]);
	}
	if (sum > 0xFFFF) {
		sum = (sum >> 16) + (sum & 0xFFFF);
	}
	return ~(sum & 0xFFFF) & 0xFFFF;
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



static void icmp_handle(struct ipv4_packet * packet, const char * src, const char * dest, fs_node_t * nic) {
	struct icmp_header * header = (void*)&packet->payload;
	if (header->type == 8 && header->code == 0) {
		printf("net: ping with %d bytes of payload\n", ntohs(packet->length));
		if (ntohs(packet->length) & 1) packet->length++;

		struct ipv4_packet * response = malloc(ntohs(packet->length));
		response->length = packet->length;
		response->destination = packet->source;
		response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
		response->ttl = 64;
		response->protocol = 1;
		response->ident = packet->ident;
		response->flags_fragment = htons(0x4000);
		response->version_ihl = 0x45;
		response->dscp_ecn = 0;
		response->checksum = 0;
		response->checksum = htons(calculate_ipv4_checksum(response));
		memcpy(response->payload, packet->payload, ntohs(packet->length));

		struct icmp_header * ping_reply = (void*)&response->payload;
		ping_reply->csum = 0;
		ping_reply->type = 0;
		ping_reply->csum = htons(icmp_checksum(response));

		/* send ipv4... */
		net_eth_send((struct EthernetDevice*)nic->device, ntohs(response->length), response, ETHERNET_TYPE_IPV4, ETHERNET_BROADCAST_MAC);
		free(response);
	} else {
		printf("net: ipv4: %s: %s -> %s ICMP %d (code = %d)\n", nic->name, src, dest, header->type, header->code);
	}
}

static hashmap_t * udp_sockets = NULL;
static hashmap_t * tcp_sockets = NULL;

#define TCP_FLAGS_FIN (1 << 0)
#define TCP_FLAGS_SYN (1 << 1)
#define TCP_FLAGS_RES (1 << 2)
#define TCP_FLAGS_PSH (1 << 3)
#define TCP_FLAGS_ACK (1 << 4)
#define TCP_FLAGS_URG (1 << 5)
#define TCP_FLAGS_ECE (1 << 6)
#define TCP_FLAGS_CWR (1 << 7)
#define TCP_FLAGS_NS  (1 << 8)
#define DATA_OFFSET_5 (0x5 << 12)

static void tcp_ack(fs_node_t * nic, sock_t * sock, struct ipv4_packet * packet) {
	sock->priv[1] = 2;

	size_t total_length = sizeof(struct ipv4_packet) + sizeof(struct tcp_header);
	struct tcp_header * tcp = (struct tcp_header*)&packet->payload;

	struct ipv4_packet * response = malloc(total_length);
	response->length = htons(total_length);
	response->destination = packet->source;
	response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
	response->ttl = 64;
	response->protocol = IPV4_PROT_TCP;
	response->ident = 0;
	response->flags_fragment = htons(0x4000);
	response->version_ihl = 0x45;
	response->dscp_ecn = 0;
	response->checksum = 0;
	response->checksum = htons(calculate_ipv4_checksum(response));

	sock->priv[2] = 1;

	/* Stick TCP header into payload */
	struct tcp_header * tcp_header = (struct tcp_header*)&response->payload;
	tcp_header->source_port = htons(sock->priv[0]);
	tcp_header->destination_port = tcp->source_port;
	tcp_header->seq_number = htonl(sock->priv[2]);
	tcp_header->ack_number = tcp->seq_number;
	tcp_header->flags = htons((TCP_FLAGS_ACK) | 0x5000);
	tcp_header->window_size = htons(1548-54);
	tcp_header->checksum = 0;
	tcp_header->urgent = 0;

	sock->priv[2]++;

	/* Calculate checksum */
	struct tcp_check_header check_hd = {
		.source = response->source,
		.destination = response->destination,
		.zeros = 0,
		.protocol = IPV4_PROT_TCP,
		.tcp_len = htons(sizeof(struct tcp_header)),
	};

	tcp_header->checksum = htons(calculate_tcp_checksum(&check_hd, tcp_header, NULL, 0));

	net_eth_send((struct EthernetDevice*)nic->device, ntohs(response->length), response, ETHERNET_TYPE_IPV4, ETHERNET_BROADCAST_MAC);

	net_sock_add(sock, packet, ntohs(packet->length));
}

void net_ipv4_handle(struct ipv4_packet * packet, fs_node_t * nic) {

	char dest[16];
	char src[16];

	ip_ntoa(ntohl(packet->destination), dest);
	ip_ntoa(ntohl(packet->source), src);

	switch (packet->protocol) {
		case 1:
			icmp_handle(packet, src, dest, nic);
			break;
		case IPV4_PROT_UDP: {
			uint16_t dest_port = ntohs(((uint16_t*)&packet->payload)[1]);
			printf("net: ipv4: %s: %s -> %s udp %d to %d\n", nic->name, src, dest, ntohs(((uint16_t*)&packet->payload)[0]), dest_port);
			if (udp_sockets && hashmap_has(udp_sockets, (void*)(uintptr_t)dest_port)) {
				printf("net: udp: received and have a waiting endpoint!\n");
				sock_t * sock = hashmap_get(udp_sockets, (void*)(uintptr_t)dest_port);
				net_sock_add(sock, packet, ntohs(packet->length));
			}
			break;
		}
		case IPV4_PROT_TCP: {
			uint16_t dest_port = ntohs(((uint16_t*)&packet->payload)[1]);
			printf("net: ipv4: %s: %s -> %s tcp %d to %d\n", nic->name, src, dest, ntohs(((uint16_t*)&packet->payload)[0]), dest_port);
			if (tcp_sockets && hashmap_has(tcp_sockets, (void*)(uintptr_t)dest_port)) {
				printf("net: tcp: received and have a waiting endpoint!\n");
				/* What kind of packet is this? Is it something we were expecting? */
				sock_t * sock = hashmap_get(tcp_sockets, (void*)(uintptr_t)dest_port);
				struct tcp_header * tcp = (struct tcp_header*)&packet->payload;

				if (sock->priv[1] == 1) {
					/* Awaiting SYN ACK, is this one? */
					if ((ntohs(tcp->flags) & (TCP_FLAGS_SYN | TCP_FLAGS_ACK)) == (TCP_FLAGS_SYN | TCP_FLAGS_ACK)) {
						printf("tcp: synack\n");
						tcp_ack(nic, sock, packet);
					}
				} else if (sock->priv[1] == 2) {
					net_sock_add(sock, packet, ntohs(packet->length));
				}
			}
			break;
		}
	}
}

extern fs_node_t * net_if_any(void);

static spin_lock_t udp_port_lock = {0};

static int next_port = 12345;
static int udp_get_port(sock_t * sock) {
	spin_lock(udp_port_lock);
	int out = next_port++;
	if (!udp_sockets) {
		udp_sockets = hashmap_create_int(10);
	}
	hashmap_set(udp_sockets, (void*)(uintptr_t)out, sock);
	sock->priv[0] = out;
	spin_unlock(udp_port_lock);
	return out;
}

static long sock_udp_send(sock_t * sock, const struct msghdr *msg, int flags) {
	printf("udp: send called\n");
	if (msg->msg_iovlen > 1) {
		printf("net: todo: can't send multiple iovs\n");
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;
	if (msg->msg_namelen != sizeof(struct sockaddr_in)) {
		printf("udp: invalid destination address size %ld\n", msg->msg_namelen);
		return -EINVAL;
	}

	if (sock->priv[0] == 0) {
		udp_get_port(sock);
		printf("udp: assigning port %d to socket\n", sock->priv[0]);
	}

	struct sockaddr_in * name = msg->msg_name;

	char dest[16];
	ip_ntoa(ntohl(name->sin_addr.s_addr), dest);
	printf("udp: want to send to %s\n", dest);

	/* Routing: We need a device to send this on... */
	fs_node_t * nic = net_if_any();

	size_t total_length = sizeof(struct ipv4_packet) + msg->msg_iov[0].iov_len + sizeof(struct udp_packet);

	struct ipv4_packet * response = malloc(total_length);
	response->length = htons(total_length);
	response->destination = name->sin_addr.s_addr;
	response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
	response->ttl = 64;
	response->protocol = IPV4_PROT_UDP;
	response->ident = 0;
	response->flags_fragment = htons(0x4000);
	response->version_ihl = 0x45;
	response->dscp_ecn = 0;
	response->checksum = 0;
	response->checksum = htons(calculate_ipv4_checksum(response));

	/* Stick UDP header into payload */
	struct udp_packet * udp_packet = (struct udp_packet*)&response->payload;
	udp_packet->source_port = htons(sock->priv[0]);
	udp_packet->destination_port = name->sin_port;
	udp_packet->length = htons(sizeof(struct udp_packet) + msg->msg_iov[0].iov_len);
	udp_packet->checksum = 0;

	memcpy(response->payload + sizeof(struct udp_packet), msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len);

	net_eth_send((struct EthernetDevice*)nic->device, ntohs(response->length), response, ETHERNET_TYPE_IPV4, ETHERNET_BROADCAST_MAC);
	free(response);

	return 0;
}

static long sock_udp_recv(sock_t * sock, struct msghdr * msg, int flags) {
	printf("udp: recv called\n");
	if (!sock->priv[0]) {
		printf("udp: recv() but socket has no port\n");
		return -EINVAL;
	}

	if (msg->msg_iovlen > 1) {
		printf("net: todo: can't recv multiple iovs\n");
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;

	struct ipv4_packet * data = net_sock_get(sock);
	printf("udp: got response, size is %u - sizeof(ipv4) - sizeof(udp) = %lu\n",
		ntohs(data->length), ntohs(data->length) - sizeof(struct ipv4_packet) - sizeof(struct udp_packet));
	memcpy(msg->msg_iov[0].iov_base, data->payload + 8, ntohs(data->length) - sizeof(struct ipv4_packet) - sizeof(struct udp_packet));

	printf("udp: data copied to iov 0, return length?\n");

	long resp = ntohs(data->length) - sizeof(struct ipv4_packet) - sizeof(struct udp_packet);
	free(data);
	return resp;
}

static long sock_tcp_send(sock_t * sock, const struct msghdr *msg, int flags) {
	printf("tcp: send called\n");
	return 0;
}

static void sock_udp_close(sock_t * sock) {
	if (sock->priv[0] && udp_sockets) {
		printf("udp: removing port %d from bound map\n", sock->priv[0]);
		spin_lock(udp_port_lock);
		hashmap_remove(udp_sockets, (void*)(uintptr_t)sock->priv[0]);
		spin_unlock(udp_port_lock);
	}
}

static int udp_socket(void) {
	printf("udp socket...\n");
	sock_t * sock = net_sock_create();
	sock->sock_recv = sock_udp_recv;
	sock->sock_send = sock_udp_send;
	sock->sock_close = sock_udp_close;
	return process_append_fd((process_t *)this_core->current_process, (fs_node_t *)sock);
}

static spin_lock_t tcp_port_lock = {0};
static void sock_tcp_close(sock_t * sock) {
	if (sock->priv[0] && tcp_sockets) {
		printf("tcp: removing port %d from bound map\n", sock->priv[0]);
		spin_lock(tcp_port_lock);
		hashmap_remove(tcp_sockets, (void*)(uintptr_t)sock->priv[0]);
		spin_unlock(tcp_port_lock);
	}
}

static int next_tcp_port = 12345;
static int tcp_get_port(sock_t * sock) {
	spin_lock(tcp_port_lock);
	int out = next_tcp_port++;
	if (!tcp_sockets) {
		tcp_sockets = hashmap_create_int(10);
	}
	hashmap_set(tcp_sockets, (void*)(uintptr_t)out, sock);
	sock->priv[0] = out;
	spin_unlock(tcp_port_lock);
	return out;
}

static long sock_tcp_recv(sock_t * sock, struct msghdr * msg, int flags) {
	printf("tcp: recv called\n");
	if (!sock->priv[0]) {
		printf("tcp: recv() but socket has no port\n");
		return -EINVAL;
	}

	if (msg->msg_iovlen > 1) {
		printf("net: todo: can't recv multiple iovs\n");
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;

	struct ipv4_packet * data = net_sock_get(sock);
	printf("tcp: got response, size is %u\n",
		ntohs(data->length));
	free(data);
	return 0;
}

static long sock_tcp_connect(sock_t * sock, const struct sockaddr *addr, socklen_t addrlen) {
	const struct sockaddr_in * dest = (const struct sockaddr_in *)addr;
	char deststr[16];
	ip_ntoa(ntohl(dest->sin_addr.s_addr), deststr);
	printf("tcp: connect requested to %s port %d\n", deststr, ntohs(dest->sin_port));

	if (sock->priv[1] != 0) {
		printf("tcp: socket is already connected?\n");
		return -EINVAL;
	}

	/* Get a port */
	tcp_get_port(sock);
	printf("tcp: connecting from ephemeral port %d\n", (int)sock->priv[0]);

	/* Mark as awaiting connection, send initial SYN */
	sock->priv[1] = 1;

	fs_node_t * nic = net_if_any();

	size_t total_length = sizeof(struct ipv4_packet) + sizeof(struct tcp_header);

	struct ipv4_packet * response = malloc(total_length);
	response->length = htons(total_length);
	response->destination = dest->sin_addr.s_addr;
	response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
	response->ttl = 64;
	response->protocol = IPV4_PROT_TCP;
	response->ident = 0;
	response->flags_fragment = htons(0x4000);
	response->version_ihl = 0x45;
	response->dscp_ecn = 0;
	response->checksum = 0;
	response->checksum = htons(calculate_ipv4_checksum(response));

	/* Stick TCP header into payload */
	struct tcp_header * tcp_header = (struct tcp_header*)&response->payload;
	tcp_header->source_port = htons(sock->priv[0]);
	tcp_header->destination_port = dest->sin_port;
	tcp_header->seq_number = 0;
	tcp_header->ack_number = 0;
	tcp_header->flags = htons((1 << 1) | 0x5000);
	tcp_header->window_size = htons(1548-54);
	tcp_header->checksum = 0;
	tcp_header->urgent = 0;

	/* Calculate checksum */
	struct tcp_check_header check_hd = {
		.source = response->source,
		.destination = response->destination,
		.zeros = 0,
		.protocol = IPV4_PROT_TCP,
		.tcp_len = htons(sizeof(struct tcp_header)),
	};

	tcp_header->checksum = htons(calculate_tcp_checksum(&check_hd, tcp_header, NULL, 0));

	/* TODO: enqueue tcp packet for potential redelivery */

	net_eth_send((struct EthernetDevice*)nic->device, ntohs(response->length), response, ETHERNET_TYPE_IPV4, ETHERNET_BROADCAST_MAC);

	free(response);

	printf("tcp: waiting for connect to finish\n");

	/* wait for signal that we connected or timed out */
	struct ipv4_packet * data = net_sock_get(sock);
	printf("tcp: connect complete\n");
	free(data);

	return 0;
}

ssize_t sock_tcp_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	printf("tcp: read into buffer of %zu bytes\n", size);
	return 0;
}
ssize_t sock_tcp_write(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	printf("tcp: write of %zu bytes\n", size);
	return 0;
}

static int tcp_socket(void) {
	printf("tcp socket...\n");
	sock_t * sock = net_sock_create();
	sock->sock_recv = sock_tcp_recv;
	sock->sock_send = sock_tcp_send;
	sock->sock_close = sock_tcp_close;
	sock->sock_connect = sock_tcp_connect;
	sock->_fnode.read = sock_tcp_read;
	sock->_fnode.write = sock_tcp_write;
	int fd = process_append_fd((process_t *)this_core->current_process, (fs_node_t *)sock);
	FD_MODE(fd) = 03;
	return fd;
}

long net_ipv4_socket(int type, int protocol) {
	/* Ignore protocol, make socket for 'type' only... */
	switch (type) {
		case SOCK_DGRAM:
			return udp_socket();
		case SOCK_STREAM:
			return tcp_socket();
		default:
			return -EINVAL;
	}
}
