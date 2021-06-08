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
#include <errno.h>

#include <sys/socket.h>

struct ethernet_packet {
	uint8_t destination[6];
	uint8_t source[6];
	uint16_t type;
	uint8_t payload[];
} __attribute__((packed)) __attribute__((aligned(2)));

#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_ARP  0x0806
#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

extern spin_lock_t net_raw_sockets_lock;
extern list_t * net_raw_sockets_list;
extern void net_sock_add(sock_t * sock, void * frame);
extern void net_ipv4_handle(void * packet, fs_node_t * nic);

void net_eth_handle(struct ethernet_packet * frame, fs_node_t * nic) {
	spin_lock(net_raw_sockets_lock);
	foreach(node, net_raw_sockets_list) {
		sock_t * sock = node->value;
		if (!sock->_fnode.device || sock->_fnode.device == nic) {
			net_sock_add(sock, frame);
		}
	}
	spin_unlock(net_raw_sockets_lock);

	struct EthernetDevice * nic_eth = nic->device;

	if (!memcmp(frame->destination, nic_eth->mac, 6) || !memcmp(frame->destination, (uint8_t[])BROADCAST_MAC, 6)) {
		/* Now pass the frame to the appropriate handler... */
		switch (ntohs(frame->type)) {
			case ETHERNET_TYPE_ARP:
				printf("net: eth: %s: rx arp packet\n", nic->name);
				break;
			case ETHERNET_TYPE_IPV4:
				printf("net: eth: %s: rx ipv4 packet\n", nic->name);
				net_ipv4_handle(&frame->payload, nic);
				break;
		}
	}

	free(frame);
}
