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

void net_ipv4_handle(struct ipv4_packet * packet, fs_node_t * nic) {
	switch (packet->protocol) {
		case 1:
			printf("net: ipv4: %s: ICMP\n", nic->name);
			break;
		case IPV4_PROT_UDP:
			printf("net: ipv4: %s: udp packet\n", nic->name);
			break;
		case IPV4_PROT_TCP:
			printf("net: ipv4: %s: tcp packet\n", nic->name);
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
