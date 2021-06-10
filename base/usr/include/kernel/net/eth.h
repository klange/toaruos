#pragma once

#include <kernel/vfs.h>
#include <kernel/mod/net.h>

#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_ARP  0x0806
#define ETHERNET_BROADCAST_MAC (uint8_t[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

#define MAC_FORMAT "%02x:%02x:%02x:%02x:%02x:%02x"
#define FORMAT_MAC(m) (m)[0], (m)[1], (m)[2], (m)[3], (m)[4], (m)[5]

void net_eth_handle(struct ethernet_packet * frame, fs_node_t * nic);

struct EthernetDevice {
	char if_name[32];
	uint8_t mac[6];

	size_t mtu;

	/* XXX: just to get things going */
	uint32_t ipv4_addr;
	uint32_t ipv4_subnet;

	uint8_t ipv6_addr[16];
	/* TODO: Address lists? */

	fs_node_t * device_node;
};

void net_eth_send(struct EthernetDevice *, size_t, void*, uint16_t, uint8_t*);
