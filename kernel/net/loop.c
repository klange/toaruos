/**
 * @file kernel/net/loop.c
 * @brief Loopback interface
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/vfs.h>
#include <kernel/spinlock.h>
#include <kernel/list.h>
#include <kernel/net/netif.h>
#include <kernel/net/eth.h>
#include <errno.h>

#include <sys/socket.h>
#include <net/if.h>

struct loop_nic {
	struct EthernetDevice eth;
	netif_counters_t counts;
};

static int ioctl_loop(fs_node_t * node, unsigned long request, void * argp) {
	struct loop_nic * nic = node->device;

	switch (request) {
		case SIOCGIFHWADDR:
			return 1;
		case SIOCGIFADDR:
			if (nic->eth.ipv4_addr == 0) return -ENOENT;
			memcpy(argp, &nic->eth.ipv4_addr, sizeof(nic->eth.ipv4_addr));
			return 0;
		case SIOCSIFADDR:
			memcpy(&nic->eth.ipv4_addr, argp, sizeof(nic->eth.ipv4_addr));
			return 0;
		case SIOCGIFNETMASK:
			if (nic->eth.ipv4_subnet == 0) return -ENOENT;
			memcpy(argp, &nic->eth.ipv4_subnet, sizeof(nic->eth.ipv4_subnet));
			return 0;
		case SIOCSIFNETMASK:
			memcpy(&nic->eth.ipv4_subnet, argp, sizeof(nic->eth.ipv4_subnet));
			return 0;
		case SIOCGIFGATEWAY:
			if (nic->eth.ipv4_subnet == 0) return -ENOENT;
			memcpy(argp, &nic->eth.ipv4_gateway, sizeof(nic->eth.ipv4_gateway));
			return 0;
		case SIOCSIFGATEWAY:
			memcpy(&nic->eth.ipv4_gateway, argp, sizeof(nic->eth.ipv4_gateway));
			net_arp_ask(nic->eth.ipv4_gateway, node);
			return 0;

		case SIOCGIFADDR6:
			return -ENOENT;
		case SIOCSIFADDR6:
			memcpy(&nic->eth.ipv6_addr, argp, sizeof(nic->eth.ipv6_addr));
			return 0;

		case SIOCGIFFLAGS: {
			uint32_t * flags = argp;
			*flags = IFF_RUNNING;
			*flags |= IFF_UP;
			*flags |= IFF_LOOPBACK;
			return 0;
		}

		case SIOCGIFMTU: {
			uint32_t * mtu = argp;
			*mtu = nic->eth.mtu;
			return 0;
		}

		case SIOCGIFCOUNTS: {
			memcpy(argp, &nic->counts, sizeof(netif_counters_t));
			return 0;
		}

		default:
			return -EINVAL;
	}
}

static ssize_t write_loop(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	struct loop_nic * nic = node->device;
	nic->counts.rx_count++;
	nic->counts.tx_count++;
	nic->counts.rx_bytes += size;
	nic->counts.tx_bytes += size;

	net_eth_handle((void*)buffer, node, size);
	return size;
}

static void loop_init(struct loop_nic * nic) {
	nic->eth.device_node = calloc(sizeof(fs_node_t),1);
	snprintf(nic->eth.device_node->name, 100, "%s", nic->eth.if_name);
	nic->eth.device_node->flags = FS_BLOCKDEVICE;
	nic->eth.device_node->mask  = 0666;
	nic->eth.device_node->ioctl = ioctl_loop;
	nic->eth.device_node->write = write_loop;
	nic->eth.device_node->device = nic;
	nic->eth.mtu = 65536; /* guess */

	nic->eth.ipv4_addr   = 0x0100007F;
	nic->eth.ipv4_subnet = 0x000000FF;

	net_add_interface(nic->eth.if_name, nic->eth.device_node);
}

fs_node_t * loopbook_install(void) {
	struct loop_nic * nic = calloc(1,sizeof(struct loop_nic));
	snprintf(nic->eth.if_name, 31, "lo");
	loop_init(nic);
	return nic->eth.device_node;
	return 0;
}

