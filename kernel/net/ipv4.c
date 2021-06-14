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

extern void net_sock_add(sock_t * sock, void * frame);

static hashmap_t * udp_sockets = NULL;

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
			printf("net: ipv4: %s: %s -> %s udp %d to %d\n", nic->name, src, dest, ntohs(((uint16_t*)&packet->payload)[0]), ntohs(((uint16_t*)&packet->payload)[1]));
			if (udp_sockets && hashmap_has(udp_sockets, (void*)(uintptr_t)dest_port)) {
				printf("net: udp: received and have a waiting endpoint!\n");
				void * tmp = malloc(ntohs(packet->length));
				memcpy(tmp, packet, ntohs(packet->length));
				sock_t * sock = hashmap_get(udp_sockets, (void*)(uintptr_t)dest_port);
				net_sock_add(sock, tmp);
			}
			break;
		}
		case IPV4_PROT_TCP:
			printf("net: ipv4: %s: %s -> %s tcp %d to %d\n", nic->name, src, dest, ntohs(((uint16_t*)&packet->payload)[0]), ntohs(((uint16_t*)&packet->payload)[1]));
			break;
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
	struct udp_packet * udp_packet = &response->payload;
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

long net_ipv4_socket(int type, int protocol) {
	/* Ignore protocol, make socket for 'type' only... */
	switch (type) {
		case SOCK_DGRAM:
			return udp_socket();
		case SOCK_STREAM:
			printf("tcp socket...\n");
			return -EINVAL;
		default:
			return -EINVAL;
	}
}
