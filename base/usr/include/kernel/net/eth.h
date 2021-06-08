#pragma once

#include <kernel/vfs.h>
#include <kernel/mod/net.h>

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
