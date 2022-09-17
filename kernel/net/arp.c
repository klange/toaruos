/**
 * @file  kernel/net/arp.c
 * @brief Address resolution
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/hashmap.h>
#include <kernel/net/netif.h>
#include <kernel/net/eth.h>

#include <sys/socket.h>

#ifndef MISAKA_DEBUG_NET
#define printf(...)
#endif

struct arp_header {
	uint16_t arp_htype;
	uint16_t arp_ptype;
	uint8_t  arp_hlen;
	uint8_t  arp_plen;
	uint16_t arp_oper;
	union {
		struct {
			uint8_t  arp_sha[6];
			uint32_t arp_spa;
			uint8_t arp_tha[6];
			uint32_t arp_tpa;
		} __attribute__((packed)) arp_eth_ipv4;
	} arp_data;
} __attribute__((packed));

static void ip_ntoa(const uint32_t src_addr, char * out) {
	snprintf(out, 16, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

spin_lock_t net_arp_cache_lock = {0};
hashmap_t * net_arp_cache = NULL;

void net_arp_cache_add(struct EthernetDevice * iface, uint32_t addr, uint8_t * hwaddr, uint16_t flags) {
	spin_lock(net_arp_cache_lock);
	struct ArpCacheEntry * entry = hashmap_get(net_arp_cache, (void*)(uintptr_t)addr);
	if (!entry) entry = malloc(sizeof(struct ArpCacheEntry));
	memcpy(entry->hwaddr, hwaddr, 6);
	entry->flags = flags;
	entry->iface = iface;
	hashmap_set(net_arp_cache, (void*)(uintptr_t)addr, entry);
	spin_unlock(net_arp_cache_lock);
}

struct ArpCacheEntry * net_arp_cache_get(uint32_t addr) {
	spin_lock(net_arp_cache_lock);
	struct ArpCacheEntry * out = hashmap_get(net_arp_cache, (void*)(uintptr_t)addr);
	spin_unlock(net_arp_cache_lock);
	return out;
}

void net_arp_ask(uint32_t addr, fs_node_t * fsnic) {
	struct EthernetDevice * ethnic = fsnic->device;
	struct arp_header arp_request = {0};

	arp_request.arp_htype = htons(1); /* Ethernet */
	arp_request.arp_ptype = htons(ETHERNET_TYPE_IPV4);
	arp_request.arp_hlen  = 6;
	arp_request.arp_plen  = 4;
	arp_request.arp_oper  = htons(1); /* Who is...? */
	arp_request.arp_data.arp_eth_ipv4.arp_tpa = addr;
	memcpy(arp_request.arp_data.arp_eth_ipv4.arp_sha, ethnic->mac, 6);

	if (ethnic->ipv4_addr) {
		arp_request.arp_data.arp_eth_ipv4.arp_spa = ethnic->ipv4_addr;
	}

	net_eth_send(ethnic, sizeof(struct arp_header), &arp_request, ETHERNET_TYPE_ARP, ETHERNET_BROADCAST_MAC);
}

void net_arp_handle(struct arp_header * packet, fs_node_t * nic) {
	printf("net: arp: hardware %d protocol %d operation %d hlen %d plen %d\n",
		ntohs(packet->arp_htype), ntohs(packet->arp_ptype), ntohs(packet->arp_oper),
		packet->arp_hlen, packet->arp_plen);
	struct EthernetDevice * eth_dev = nic->device;

	if (ntohs(packet->arp_htype) == 1 && ntohs(packet->arp_ptype) == ETHERNET_TYPE_IPV4) {
		/* Ethernet, IPv4 */
		if (packet->arp_data.arp_eth_ipv4.arp_spa) {
			net_arp_cache_add(eth_dev, packet->arp_data.arp_eth_ipv4.arp_spa, packet->arp_data.arp_eth_ipv4.arp_sha, 0);
		}
		if (ntohs(packet->arp_oper) == 1) {
			char spa[17];
			ip_ntoa(ntohl(packet->arp_data.arp_eth_ipv4.arp_spa), spa);
			char tpa[17];
			ip_ntoa(ntohl(packet->arp_data.arp_eth_ipv4.arp_tpa), tpa);
			printf("net: arp: " MAC_FORMAT " (%s) wants to know who %s is\n",
				FORMAT_MAC(packet->arp_data.arp_eth_ipv4.arp_sha),
				spa, tpa);
			if (eth_dev->ipv4_addr &&  packet->arp_data.arp_eth_ipv4.arp_tpa == eth_dev->ipv4_addr) {
				printf("net: arp: that's us, we should reply...\n");

				struct arp_header response = {0};
				response.arp_htype = htons(1);
				response.arp_ptype = htons(ETHERNET_TYPE_IPV4);
				response.arp_hlen = 6;
				response.arp_plen = 4;
				response.arp_oper = htons(2);
				memcpy(response.arp_data.arp_eth_ipv4.arp_sha, eth_dev->mac, 6);
				memcpy(response.arp_data.arp_eth_ipv4.arp_tha, packet->arp_data.arp_eth_ipv4.arp_sha, 6);
				response.arp_data.arp_eth_ipv4.arp_spa = eth_dev->ipv4_addr;
				response.arp_data.arp_eth_ipv4.arp_tpa = packet->arp_data.arp_eth_ipv4.arp_spa;
				net_eth_send(eth_dev, sizeof(struct arp_header), &response, ETHERNET_TYPE_ARP, packet->arp_data.arp_eth_ipv4.arp_sha);
			}
		} else if (ntohs(packet->arp_oper) == 2) {
			char spa[17];
			ip_ntoa(ntohl(packet->arp_data.arp_eth_ipv4.arp_spa), spa);
			printf("net: arp: " MAC_FORMAT " says they are %s\n",
				FORMAT_MAC(packet->arp_data.arp_eth_ipv4.arp_sha),
				spa);
		}
	}
}
