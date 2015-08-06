#ifndef _IPV4_H
#define _IPV4_H

#include <system.h>

struct ethernet_packet {
	uint8_t destination[6];
	uint8_t source[6];
	uint16_t type;
	uint8_t payload[];
} __attribute__((packed));

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
} __attribute__ ((packed));

struct udp_packet {
	uint16_t source_port;
	uint16_t destination_port;
	uint16_t length;
	uint16_t checksum;
	uint8_t  payload[];
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

	uint8_t  chaddr[16];

	uint8_t sname[64];
	uint8_t file[128];

	uint32_t magic;

	uint8_t  options[];
} __attribute__ ((packed));

struct dns_packet {
	uint16_t qid;
	uint16_t flags;
	uint16_t questions;
	uint16_t answers;
	uint16_t authorities;
	uint16_t additional;
	uint8_t data[];
} __attribute__ ((packed));

struct tcp_header {
	uint16_t source_port;
	uint16_t destination_port;

	uint32_t seq_number;
	uint32_t ack_number;

	uint16_t flags;
	uint16_t window_size;
	uint16_t checksum;
	uint16_t urgent;

	uint8_t  payload[];
} __attribute__((packed));

struct tcp_check_header {
	uint32_t source;
	uint32_t destination;
	uint8_t  zeros;
	uint8_t  protocol;
	uint16_t tcp_len;
	uint8_t  tcp_header[];
};

#define SOCK_STREAM 1
#define SOCK_DGRAM 2

// Note: Data offset is in upper 4 bits of flags field. Shift and subtract 5 since that is the min TCP size.
//       If the value is more than 5, multiply by 4 because this field is specified in number of words
#define TCP_OPTIONS_LENGTH(tcp) (((((tcp)->flags) >> 12) - 5) * 4)
#define TCP_HEADER_LENGTH(tcp) ((((tcp)->flags) >> 12) * 4)
#define TCP_HEADER_LENGTH_FLIPPED(tcp) (((htons((tcp)->flags)) >> 12) * 4)

#define htonl(l)  ( (((l) & 0xFF) << 24) | (((l) & 0xFF00) << 8) | (((l) & 0xFF0000) >> 8) | (((l) & 0xFF000000) >> 24))
#define htons(s)  ( (((s) & 0xFF) << 8) | (((s) & 0xFF00) >> 8) )
#define ntohl(l)  htonl((l))
#define ntohs(s)  htons((s))

#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define IPV4_PROT_UDP 17
#define IPV4_PROT_TCP 6
#define DHCP_MAGIC 0x63825363

#define TCP_FLAGS_FIN (1 << 0)
#define TCP_FLAGS_SYN (1 << 1)
#define TCP_FLAGS_RES (1 << 2)
#define TCP_FLAGS_PSH (1 << 3)
#define TCP_FLAGS_ACK (1 << 4)
#define TCP_FLAGS_URG (1 << 5)
#define TCP_FLAGS_ECE (1 << 6)
#define TCP_FLAGS_CWR (1 << 7)
#define TCP_FLAGS_NS  (1 << 8)
#define DATA_OFFSET_5 (0x5 << 12)

#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_ARP  0x0806

extern uint32_t ip_aton(const char * in);
extern void ip_ntoa(uint32_t src_addr, char * out);
extern uint16_t calculate_ipv4_checksum(struct ipv4_packet * p);
uint16_t calculate_tcp_checksum(struct tcp_check_header * p, struct tcp_header * h, void * d, size_t d_words);

struct tcp_socket {
	list_t* is_connected;
	uint32_t seq_no;
	uint32_t ack_no;
	int status;
};

// Note: for now, not sure what to put in here, so removing from the union to get rid of compiler warnings about empty struct
// struct udp_socket {
// };

struct socket {
	uint32_t ip;
	uint8_t  mac[6];
	uint32_t port_dest;
	uint32_t port_recv;
	list_t* packet_queue;
	spin_lock_t packet_queue_lock;
	list_t* packet_wait;
	int32_t status;
	size_t bytes_available;
	size_t bytes_read;
	void * current_packet;
	uint32_t sock_type;
	union {
		struct tcp_socket tcp_socket;
		// struct udp_socket udp_socket;
	} proto_sock;
};

struct sized_blob {
	size_t  size;
	uint8_t blob[];
};

struct in_addr {
	unsigned long s_addr;          // load with inet_pton()
};

struct sockaddr {
	uint16_t 	sa_family;
	char 		sa_data[14];
};

struct sockaddr_in {
	short			sin_family;   // e.g. AF_INET, AF_INET6
	unsigned short	sin_port;     // e.g. htons(3490)
	struct in_addr	sin_addr;     // see struct in_addr, below
	char			sin_zero[8];  // zero this if you want to
};

typedef struct {
	uint8_t	*payload;
	size_t	payload_size;
} tcpdata_t;

#endif
