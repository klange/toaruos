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

void net_ipv4_handle(struct ipv4_packet * packet, fs_node_t * nic) {

	char dest[16];
	char src[16];

	ip_ntoa(ntohl(packet->destination), dest);
	ip_ntoa(ntohl(packet->source), src);

	switch (packet->protocol) {
		case 1:
			icmp_handle(packet, src, dest, nic);
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
