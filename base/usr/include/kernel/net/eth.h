#pragma once

#include <kernel/vfs.h>

#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_ARP  0x0806
#define ETHERNET_BROADCAST_MAC (uint8_t[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

#define MAC_FORMAT "%02x:%02x:%02x:%02x:%02x:%02x"
#define FORMAT_MAC(m) (m)[0], (m)[1], (m)[2], (m)[3], (m)[4], (m)[5]

struct ethernet_packet {
	uint8_t destination[6];
	uint8_t source[6];
	uint16_t type;
	uint8_t payload[];
} __attribute__((packed)) __attribute__((aligned(2)));

void net_eth_handle(struct ethernet_packet * frame, fs_node_t * nic, size_t size);

struct EthernetDevice {
	char if_name[32];
	uint8_t mac[6];

	size_t mtu;

	/* XXX: just to get things going */
	uint32_t ipv4_addr;
	uint32_t ipv4_subnet;
	uint32_t ipv4_gateway;

	uint8_t ipv6_addr[16];
	/* TODO: Address lists? */

	fs_node_t * device_node;
};

void net_eth_send(struct EthernetDevice *, size_t, void*, uint16_t, uint8_t*);

struct ArpCacheEntry {
	uint8_t hwaddr[6];
	uint16_t flags;
	struct EthernetDevice * iface;
};

struct ArpCacheEntry * net_arp_cache_get(uint32_t addr);
void net_arp_cache_add(struct EthernetDevice * iface, uint32_t addr, uint8_t * hwaddr, uint16_t flags);
void net_arp_ask(uint32_t addr, fs_node_t * fsnic);

