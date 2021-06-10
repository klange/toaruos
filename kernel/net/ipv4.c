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
#include <kernel/printf.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>

#include <kernel/net/netif.h>

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

#define IPV4_PROT_UDP 17
#define IPV4_PROT_TCP 6

static void ip_ntoa(const uint32_t src_addr, char * out) {
	snprintf(out, 16, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

void net_ipv4_handle(struct ipv4_packet * packet, fs_node_t * nic) {

	char dest[16];
	char src[16];

	ip_ntoa(ntohl(packet->destination), dest);
	ip_ntoa(ntohl(packet->source), src);

	switch (packet->protocol) {
		case 1:
			printf("net: ipv4: %s: %s -> %s ICMP\n", nic->name, src, dest);
			break;
		case IPV4_PROT_UDP:
			printf("net: ipv4: %s: %s -> %s udp %d to %d\n", nic->name, src, dest, ntohs(((uint16_t*)&packet->payload)[0]), ntohs(((uint16_t*)&packet->payload)[1]));
			break;
		case IPV4_PROT_TCP:
			printf("net: ipv4: %s: %s -> %s tcp %d to %d\n", nic->name, src, dest, ntohs(((uint16_t*)&packet->payload)[0]), ntohs(((uint16_t*)&packet->payload)[1]));
			break;
	}
}

long net_ipv4_socket(int type, int protocol) {
	/* Ignore protocol, make socket for 'type' only... */
	switch (type) {
		case SOCK_DGRAM:
			printf("udp socket...\n");
			return -EINVAL;
		case SOCK_STREAM:
			printf("tcp socket...\n");
			return -EINVAL;
		default:
			return -EINVAL;
	}
}
