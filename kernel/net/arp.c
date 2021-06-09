#include <errno.h>
#include <kernel/types.h>
#include <kernel/printf.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/mod/net.h>
#include <kernel/net/netif.h>
#include <kernel/net/eth.h>

#include <sys/socket.h>

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
			uint16_t arp_tha[6];
			uint32_t arp_tpa;
		} arp_eth_ipv4;
	} arp_data;
} __attribute__((packed));

static void ip_ntoa(const uint32_t src_addr, char * out) {
	snprintf(out, 16, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

#define MAC_FORMAT "%02x:%02x:%02x:%02x:%02x:%02x"
#define FORMAT_MAC(m) (m)[0], (m)[1], (m)[2], (m)[3], (m)[4], (m)[5]

void net_arp_handle(struct arp_header * packet, fs_node_t * nic) {
	printf("net: arp: hardware %d protocol %d operation %d\n",
		ntohs(packet->arp_htype), ntohs(packet->arp_ptype), ntohs(packet->arp_oper));

	if (ntohs(packet->arp_htype) == 1 && ntohs(packet->arp_ptype) == 0x0800) {
		/* Ethernet, IPv4 */
		if (ntohs(packet->arp_oper) == 1) {
			char spa[17];
			ip_ntoa(ntohl(packet->arp_data.arp_eth_ipv4.arp_spa), spa);
			char tpa[17];
			ip_ntoa(ntohl(packet->arp_data.arp_eth_ipv4.arp_tpa), tpa);
			printf("net: arp: " MAC_FORMAT " (%s) wants to know who %s is\n",
				FORMAT_MAC(packet->arp_data.arp_eth_ipv4.arp_sha),
				spa, tpa);
		} else if (ntohs(packet->arp_oper) == 2) {
			char spa[17];
			ip_ntoa(ntohl(packet->arp_data.arp_eth_ipv4.arp_spa), spa);
			printf("net: arp: " MAC_FORMAT " says they are %s\n",
				FORMAT_MAC(packet->arp_data.arp_eth_ipv4.arp_sha),
				spa);
		}
	}
}
