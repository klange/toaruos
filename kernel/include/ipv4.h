#ifndef _IPV4_H
#define _IPV4_H

#include <system.h>

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
} __attribute__ ((packed));

struct udp_packet {
	uint32_t source;
	uint32_t destination;
	uint8_t  zeroes;
	uint8_t  protocol;
	uint8_t  udp_length;
	uint16_t source_port;
	uint16_t destination_port;
	uint16_t length;
	uint16_t checksum;
} __attribute__ ((packed));

struct dhcp_packet {

	uint8_t op;
	uint8_t htype;
	uint8_t hlen;
	uint8_t hops;

	uint32_t xid;

	uint16_t secs;
	uint16_t flags;

	uint32_t ciaddr;
	uint32_t yiaddr;
	uint32_t siaddr;
	uint32_t giaddr;

	uint32_t chaddr0;
	uint32_t chaddr1;
	uint32_t chaddr2;
	uint32_t chaddr3;

	uint8_t sname[64];
	uint8_t file[128];

	uint32_t magic;



} __attribute__ ((packed));

#endif
