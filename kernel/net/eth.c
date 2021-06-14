#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/pipe.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/vfs.h>
#include <kernel/mod/net.h>
#include <kernel/net/netif.h>
#include <kernel/net/eth.h>
#include <kernel/net/ipv4.h>
#include <errno.h>

#include <sys/socket.h>

struct ethernet_packet {
	uint8_t destination[6];
	uint8_t source[6];
	uint16_t type;
	uint8_t payload[];
} __attribute__((packed)) __attribute__((aligned(2)));

extern spin_lock_t net_raw_sockets_lock;
extern list_t * net_raw_sockets_list;
extern void net_ipv4_handle(void * packet, fs_node_t * nic);
extern void net_arp_handle(void * packet, fs_node_t * nic);

void net_eth_handle(struct ethernet_packet * frame, fs_node_t * nic) {
	spin_lock(net_raw_sockets_lock);
	foreach(node, net_raw_sockets_list) {
		sock_t * sock = node->value;
		if (!sock->_fnode.device || sock->_fnode.device == nic) {
			net_sock_add(sock, frame, 8092);
		}
	}
	spin_unlock(net_raw_sockets_lock);

	struct EthernetDevice * nic_eth = nic->device;

	if (!memcmp(frame->destination, nic_eth->mac, 6) || !memcmp(frame->destination, ETHERNET_BROADCAST_MAC, 6)) {
		/* Now pass the frame to the appropriate handler... */
		switch (ntohs(frame->type)) {
			case ETHERNET_TYPE_ARP:
				net_arp_handle(&frame->payload, nic);
				break;
			case ETHERNET_TYPE_IPV4: {
				struct ipv4_packet * packet = (struct ipv4_packet*)&frame->payload;
				printf("net: eth: %s: rx ipv4 packet\n", nic->name);
				if (packet->source != 0xFFFFFFFF) {
					net_arp_cache_add(nic->device, packet->source, frame->source, 0);
				}
				net_ipv4_handle(packet, nic);
				break;
			}
		}
	}

	free(frame);
}

void net_eth_send(struct EthernetDevice * nic, size_t len, void* data, uint16_t type, uint8_t * dest) {
	size_t total_size = sizeof(struct ethernet_packet) + len;
	struct ethernet_packet * packet = malloc(total_size);
	memcpy(packet->payload, data, len);
	memcpy(packet->destination, dest, 6);
	memcpy(packet->source, nic->mac, 6);
	packet->type = htons(type);
	write_fs(nic->device_node, 0, total_size, (uint8_t*)packet);
	free(packet);
}
