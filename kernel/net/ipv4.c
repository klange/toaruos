/**
 * @file  kernel/net/ipv4.c
 * @brief IPv4, TCP, UDP protocol implementation.
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
#include <kernel/hashmap.h>
#include <kernel/vfs.h>
#include <kernel/time.h>
#include <kernel/misc.h>
#include <kernel/assert.h>

#include <kernel/net/netif.h>
#include <kernel/net/eth.h>
#include <kernel/net/ipv4.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#ifndef MISAKA_DEBUG_NET
#define printf(...) if (_debug) printf(__VA_ARGS__)
//#define printf(...)
#endif

#define DEFAULT_TCP_WINDOW_SIZE 65535

static int _debug __attribute__((unused)) = 0;

static void ip_ntoa(const uint32_t src_addr, char * out) {
	snprintf(out, 16, "%d.%d.%d.%d",
		(src_addr & 0xFF000000) >> 24,
		(src_addr & 0xFF0000) >> 16,
		(src_addr & 0xFF00) >> 8,
		(src_addr & 0xFF));
}

static uint16_t icmp_checksum(struct ipv4_packet * packet) {
	uint32_t sum = 0;
	uint16_t * s = (uint16_t *)packet->payload;
	for (int i = 0; i < (ntohs(packet->length) - 20) / 2; ++i) {
		sum += ntohs(s[i]);
	}
	if (sum > 0xFFFF) {
		sum = (sum >> 16) + (sum & 0xFFFF);
	}
	return ~(sum & 0xFFFF) & 0xFFFF;
}

uint16_t calculate_ipv4_checksum(struct ipv4_packet * p) {
	uint32_t sum = 0;
	uint16_t * s = (uint16_t *)p;

	/* TODO: Checksums for options? */
	for (int i = 0; i < 10; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}


	return ~(sum & 0xFFFF) & 0xFFFF;
}

uint16_t calculate_tcp_checksum(struct tcp_check_header * p, struct tcp_header * h, void * d, size_t payload_size) {
	uint32_t sum = 0;
	uint16_t * s = (uint16_t *)p;

	/* TODO: Checksums for options? */
	for (int i = 0; i < 6; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	s = (uint16_t *)h;
	for (int i = 0; i < 10; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	uint16_t d_words = payload_size / 2;

	s = (uint16_t *)d;
	for (unsigned int i = 0; i < d_words; ++i) {
		sum += ntohs(s[i]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	if (d_words * 2 != payload_size) {
		uint8_t * t = (uint8_t *)d;
		uint8_t tmp[2];
		tmp[0] = t[d_words * sizeof(uint16_t)];
		tmp[1] = 0;

		uint16_t * f = (uint16_t *)tmp;

		sum += ntohs(f[0]);
		if (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}
	}

	return ~(sum & 0xFFFF) & 0xFFFF;
}

int net_ipv4_send(struct ipv4_packet * response, fs_node_t * nic) {
	/* TODO: This should be routing, with a _hint_ about the interface, not the actual nic to send from! */
	struct EthernetDevice * enic = nic->device;

	/* where are we going? */
	uint32_t ipdest = response->destination;

	/* Get the ethernet address of the destination */
	struct ArpCacheEntry * resp;

	/* Is this local or should we send it to the gateway? */
	if (!enic->ipv4_subnet || ((ipdest & enic->ipv4_subnet) != (enic->ipv4_addr & enic->ipv4_subnet))) {
		ipdest = enic->ipv4_gateway;
		resp = net_arp_cache_get(ipdest);
	} else {
		resp = net_arp_cache_get(ipdest);
		if (!resp) {
			net_arp_ask(ipdest, nic);

			unsigned long s, ss;
			relative_time(0, 1000, &s, &ss);
			sleep_until((process_t *)this_core->current_process, s, ss);
			switch_task(0);

			resp = net_arp_cache_get(ipdest);
		}
	}


	/* Pass the packet to the next stage */
	net_eth_send(enic, ntohs(response->length), response, ETHERNET_TYPE_IPV4, resp ? resp->hwaddr : ETHERNET_BROADCAST_MAC);

	return 0;
}

static sock_t * icmp_handler = NULL;

static void icmp_handle(struct ipv4_packet * packet, const char * src, const char * dest, fs_node_t * nic) {
	struct icmp_header * header = (void*)&packet->payload;

	/* Is this a PING request? */
	if (header->type == 8 && header->code == 0) {
		printf("net: ping with %d bytes of payload\n", ntohs(packet->length));
		if (ntohs(packet->length) & 1) {
			packet->length = htons(ntohs(packet->length) + 1);
		}

		struct ipv4_packet * response = malloc(ntohs(packet->length));
		memcpy(response, packet, ntohs(packet->length));
		response->length = packet->length;
		response->destination = packet->source;
		response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
		response->ttl = 64;
		response->protocol = 1;
		response->ident = packet->ident;
		response->flags_fragment = htons(0x4000);
		response->version_ihl = 0x45;
		response->dscp_ecn = 0;
		response->checksum = 0;
		response->checksum = htons(calculate_ipv4_checksum(response));

		struct icmp_header * ping_reply = (void*)&response->payload;
		ping_reply->csum = 0;
		ping_reply->type = 0;
		ping_reply->csum = htons(icmp_checksum(response));

		/* send ipv4... */
		net_ipv4_send(response,nic);
		free(response);
	} else if (header->type == 0 && header->code == 0) {
		printf("net: ping reply\n");
		/* Did we have a client waiting for this? */
		if (icmp_handler) {
			net_sock_add(icmp_handler, packet, ntohs(packet->length));
		}
	} else {
		printf("net: ipv4: %s: %s -> %s ICMP %d (code = %d)\n", nic->name, src, dest, header->type, header->code);
	}
}

static void sock_icmp_close(sock_t * sock) {
	icmp_handler = NULL;
}

static long sock_icmp_recv(sock_t * sock, struct msghdr * msg, int flags) {
	if (msg->msg_iovlen > 1) {
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;

	if (!sock->rx_queue->length && sock->nonblocking) return -EAGAIN;

	char * packet = net_sock_get(sock);
	if (!packet) return -EINTR;
	size_t packet_size = *(size_t*)packet - sizeof(struct ipv4_packet);

	struct ipv4_packet * src = (struct ipv4_packet*)(packet + sizeof(size_t));

	if (packet_size > msg->msg_iov[0].iov_len) {
		dprintf("ICMP recv too big for vector\n");
		packet_size = msg->msg_iov[0].iov_len;
	}

	if (msg->msg_namelen == sizeof(struct sockaddr_in)) {
		if (msg->msg_name) {
			((struct sockaddr_in*)msg->msg_name)->sin_family = AF_INET;
			((struct sockaddr_in*)msg->msg_name)->sin_port = 0;
			((struct sockaddr_in*)msg->msg_name)->sin_addr.s_addr = src->source;
			((struct sockaddr_in*)msg->msg_name)->sin_zero[0] = src->ttl;
		}
	}

	memcpy(msg->msg_iov[0].iov_base, src->payload, packet_size);
	free(packet);
	return packet_size;
}

static long sock_icmp_send(sock_t * sock, const struct msghdr *msg, int flags) {
	if (msg->msg_iovlen > 1) {
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;
	if (msg->msg_namelen != sizeof(struct sockaddr_in)) {
		return -EINVAL;
	}

	struct sockaddr_in * name = msg->msg_name;
	fs_node_t * nic = net_if_route(name->sin_addr.s_addr);
	if (!nic) return -ENONET;
	size_t total_length = sizeof(struct ipv4_packet) + msg->msg_iov[0].iov_len;

	struct ipv4_packet * response = malloc(total_length);
	response->length = htons(total_length);
	response->destination = name->sin_addr.s_addr;
	response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
	response->ttl = 64;
	response->protocol = 1;
	response->ident = 0;
	response->flags_fragment = htons(0x4000);
	response->version_ihl = 0x45;
	response->dscp_ecn = 0;
	response->checksum = 0;
	response->checksum = htons(calculate_ipv4_checksum(response));

	memcpy(response->payload, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len);
	net_ipv4_send(response,nic);
	free(response);

	return 0;
}

static int icmp_socket(void) {
	printf("icmp socket...\n");
	if (icmp_handler) return -EINVAL;
	sock_t * sock = net_sock_create();
	sock->sock_recv = sock_icmp_recv;
	sock->sock_send = sock_icmp_send;
	sock->sock_close = sock_icmp_close;
	icmp_handler = sock;
	return process_append_fd((process_t *)this_core->current_process, (fs_node_t *)sock);
}

static hashmap_t * udp_sockets = NULL;
static hashmap_t * tcp_sockets = NULL;

void ipv4_install(void) {
	udp_sockets = hashmap_create_int(10);
	tcp_sockets = hashmap_create_int(10);
}

#define TCP_FLAGS_FIN (1 << 0)
#define TCP_FLAGS_SYN (1 << 1)
#define TCP_FLAGS_RST (1 << 2)
#define TCP_FLAGS_PSH (1 << 3)
#define TCP_FLAGS_ACK (1 << 4)
#define TCP_FLAGS_URG (1 << 5)
#define TCP_FLAGS_ECE (1 << 6)
#define TCP_FLAGS_CWR (1 << 7)
#define TCP_FLAGS_NS  (1 << 8)
#define DATA_OFFSET_5 (0x5 << 12)

static int tcp_ack(fs_node_t * nic, sock_t * sock, struct ipv4_packet * packet, int isSynAck, size_t payload_len) {
	struct tcp_header * tcp = (struct tcp_header*)&packet->payload;
	int retval = 1;
	int window_size = DEFAULT_TCP_WINDOW_SIZE;
	int send_thrice = 0;

#if 0
	/* XXX: This means the header is bigger than we expect... */
	if ((ntohs(tcp->flags) & 0xF000) != 0x5000) {
		int _debug __attribute__((unused)) = 1;
		printf("tcp: uh, weird flags? %#4x\n", ntohs(tcp->flags));
	}
#endif

	if (sock->priv32[1] != 0 && !isSynAck &&
		sock->priv32[1] != ntohl(tcp->seq_number)) {
#if 0
		int _debug __attribute__((unused)) = 1;
		printf("tcp: suspicious of their seq number?\n");
		printf("tcp: their seq = %u our ack = %u\n",
			ntohl(tcp->seq_number), sock->priv32[1]);
#endif
		//window_size = 300;
		
		retval = 0;
		send_thrice = 1;
	} else {
		sock->priv32[0] = isSynAck ? 1 : sock->priv32[0];
		sock->priv32[1] = (ntohl(tcp->seq_number) + payload_len) & 0xFFFFFFFF;
		sock->priv[1] = 2;
	}

	sock->priv[2]++;

#if 0
	printf("tcp: their ack = %u our seq = %u\n",
		ntohl(tcp->ack_number), sock->priv32[0]);
	printf("tcp: their seq = %u our ack = %u\n",
		ntohl(tcp->seq_number), sock->priv32[1]);
#endif


	size_t total_length = sizeof(struct ipv4_packet) + sizeof(struct tcp_header);

	struct ipv4_packet * response = malloc(total_length);
	response->length = htons(total_length);
	response->destination = packet->source;
	response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
	response->ttl = 64;
	response->protocol = IPV4_PROT_TCP;
	response->ident = htons(sock->priv[2]);
	response->flags_fragment = htons(0x0);
	response->version_ihl = 0x45;
	response->dscp_ecn = 0;
	response->checksum = 0;
	response->checksum = htons(calculate_ipv4_checksum(response));

	int flags = TCP_FLAGS_ACK;
	if (ntohs(tcp->flags) & TCP_FLAGS_FIN) {
		/* Other side is closed now */
		sock->priv32[1]++;
		sock->priv[1] = 3;
	}

	/* Stick TCP header into payload */
	struct tcp_header * tcp_header = (struct tcp_header*)&response->payload;
	tcp_header->source_port = htons(sock->priv[0]);
	tcp_header->destination_port = tcp->source_port;
	tcp_header->seq_number = htonl(sock->priv32[0]);
	tcp_header->ack_number = htonl(sock->priv32[1]);
	tcp_header->flags = htons(flags | 0x5000);
	tcp_header->window_size = htons(window_size);
	tcp_header->checksum = 0;
	tcp_header->urgent = 0;

	/* Calculate checksum */
	struct tcp_check_header check_hd = {
		.source = response->source,
		.destination = response->destination,
		.zeros = 0,
		.protocol = IPV4_PROT_TCP,
		.tcp_len = htons(sizeof(struct tcp_header)),
	};

	tcp_header->checksum = htons(calculate_tcp_checksum(&check_hd, tcp_header, NULL, 0));
	net_ipv4_send(response,nic);
	if (send_thrice) {
		net_ipv4_send(response,nic);
		net_ipv4_send(response,nic);
	}
	free(response);
	return retval;
}

void net_ipv4_handle(struct ipv4_packet * packet, fs_node_t * nic, size_t size) {

	if (size < sizeof(struct ipv4_packet)) {
		dprintf("ipv4: Incoming packet is too small.\n");
	}

	char dest[16];
	char src[16];

	ip_ntoa(ntohl(packet->destination), dest);
	ip_ntoa(ntohl(packet->source), src);

	switch (packet->protocol) {
		case 1:
			icmp_handle(packet, src, dest, nic);
			break;
		case IPV4_PROT_UDP: {
			uint16_t dest_port = ntohs(((uint16_t*)&packet->payload)[1]);
			printf("net: ipv4: %s: %s -> %s udp %d to %d\n", nic->name, src, dest, ntohs(((uint16_t*)&packet->payload)[0]), dest_port);
			if (hashmap_has(udp_sockets, (void*)(uintptr_t)dest_port)) {
				printf("net: udp: received and have a waiting endpoint!\n");
				sock_t * sock = hashmap_get(udp_sockets, (void*)(uintptr_t)dest_port);
				net_sock_add(sock, packet, ntohs(packet->length));
			}
			break;
		}
		case IPV4_PROT_TCP: {
			uint16_t dest_port = ntohs(((uint16_t*)&packet->payload)[1]);
			printf("net: ipv4: %s: %s -> %s tcp %d to %d\n", nic->name, src, dest, ntohs(((uint16_t*)&packet->payload)[0]), dest_port);
			sock_t * sock = hashmap_get(tcp_sockets, (void*)(uintptr_t)dest_port);
			if (sock) {
				printf("net: tcp: received and have a waiting endpoint!\n");
				/* What kind of packet is this? Is it something we were expecting? */
				struct tcp_header * tcp = (struct tcp_header*)&packet->payload;

				if (sock->priv[1] == 1) {
					/* Awaiting SYN ACK, is this one? */
					if ((ntohs(tcp->flags) & (TCP_FLAGS_SYN | TCP_FLAGS_ACK)) == (TCP_FLAGS_SYN | TCP_FLAGS_ACK)) {
						printf("tcp: synack\n");
						if (tcp_ack(nic, sock, packet, 1, 1)) {
							net_sock_add(sock, packet, ntohs(packet->length));
						}
					} else if ((ntohs(tcp->flags) & (TCP_FLAGS_RST))) {
						sock->priv[1] = 0;
						net_sock_alert(sock);
					}
				} else if (sock->priv[1] == 2) {
					size_t packet_len = ntohs(packet->length) - sizeof(struct ipv4_packet);
					size_t hlen = ((ntohs(tcp->flags) & 0xF000) >> 12) * 4;
					size_t payload_len = packet_len - hlen;
					if (payload_len) {
						printf("tcp: acking because payload_len = %zu (hlen=%zu, packet_len=%zu)\n", payload_len, hlen, packet_len);
						if (tcp_ack(nic, sock, packet, 0, payload_len)) {
							net_sock_add(sock, packet, ntohs(packet->length));
						}
					} else if (ntohs(tcp->flags) & TCP_FLAGS_FIN) {
						tcp_ack(nic, sock, packet, 0, 0);
					}
				}
			}
			break;
		}
	}
}

static spin_lock_t udp_port_lock = {0};

static int next_port = 12345;
static int udp_get_port(sock_t * sock) {
	spin_lock(udp_port_lock);
	int out = next_port++;
	hashmap_set(udp_sockets, (void*)(uintptr_t)out, sock);
	sock->priv[0] = out;
	spin_unlock(udp_port_lock);
	return out;
}

static long sock_udp_send(sock_t * sock, const struct msghdr *msg, int flags) {
	printf("udp: send called\n");
	if (msg->msg_iovlen > 1) {
		printf("net: todo: can't send multiple iovs\n");
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;
	if (msg->msg_namelen != sizeof(struct sockaddr_in)) {
		printf("udp: invalid destination address size %ld\n", msg->msg_namelen);
		return -EINVAL;
	}

	if (sock->priv[0] == 0) {
		udp_get_port(sock);
		printf("udp: assigning port %d to socket\n", sock->priv[0]);
	}

	struct sockaddr_in * name = msg->msg_name;

	char dest[16];
	ip_ntoa(ntohl(name->sin_addr.s_addr), dest);
	printf("udp: want to send to %s\n", dest);

	/* Routing: We need a device to send this on... */
	fs_node_t * nic = net_if_route(name->sin_addr.s_addr);
	if (!nic) return 0;

	size_t total_length = sizeof(struct ipv4_packet) + msg->msg_iov[0].iov_len + sizeof(struct udp_packet);

	struct ipv4_packet * response = malloc(total_length);
	response->length = htons(total_length);
	response->destination = name->sin_addr.s_addr;
	response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
	response->ttl = 64;
	response->protocol = IPV4_PROT_UDP;
	response->ident = 0;
	response->flags_fragment = htons(0x4000);
	response->version_ihl = 0x45;
	response->dscp_ecn = 0;
	response->checksum = 0;
	response->checksum = htons(calculate_ipv4_checksum(response));

	/* Stick UDP header into payload */
	struct udp_packet * udp_packet = (struct udp_packet*)&response->payload;
	udp_packet->source_port = htons(sock->priv[0]);
	udp_packet->destination_port = name->sin_port;
	udp_packet->length = htons(sizeof(struct udp_packet) + msg->msg_iov[0].iov_len);
	udp_packet->checksum = 0;

	memcpy(response->payload + sizeof(struct udp_packet), msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len);
	net_ipv4_send(response,nic);
	free(response);

	return msg->msg_iov[0].iov_len;
}

static long sock_udp_recv(sock_t * sock, struct msghdr * msg, int flags) {
	printf("udp: recv called\n");
	if (!sock->priv[0]) {
		printf("udp: recv() but socket has no port\n");
		return -EINVAL;
	}

	if (msg->msg_iovlen > 1) {
		printf("net: todo: can't recv multiple iovs\n");
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;

	if (!sock->rx_queue->length && sock->nonblocking) return -EAGAIN;

	char * packet = net_sock_get(sock);
	if (!packet) return -EINTR;
	struct ipv4_packet * data = (struct ipv4_packet*)(packet + sizeof(size_t));
	struct udp_packet * udp_packet = (struct udp_packet*)&data->payload;

	printf("udp: got response, size is %u - sizeof(ipv4) - sizeof(udp) = %lu\n",
		ntohs(data->length), ntohs(data->length) - sizeof(struct ipv4_packet) - sizeof(struct udp_packet));
	memcpy(msg->msg_iov[0].iov_base, udp_packet->payload, ntohs(data->length) - sizeof(struct ipv4_packet) - sizeof(struct udp_packet));

	if (msg->msg_namelen == sizeof(struct sockaddr_in)) {
		if (msg->msg_name) {
			((struct sockaddr_in*)msg->msg_name)->sin_family = AF_INET;
			((struct sockaddr_in*)msg->msg_name)->sin_port = udp_packet->source_port;
			((struct sockaddr_in*)msg->msg_name)->sin_addr.s_addr = data->source;
		}
	}

	printf("udp: data copied to iov 0, return length?\n");

	long resp = ntohs(data->length) - sizeof(struct ipv4_packet) - sizeof(struct udp_packet);
	free(packet);
	return resp;
}

static void sock_udp_close(sock_t * sock) {
	if (sock->priv[0]) {
		printf("udp: removing port %d from bound map\n", sock->priv[0]);
		spin_lock(udp_port_lock);
		hashmap_remove(udp_sockets, (void*)(uintptr_t)sock->priv[0]);
		spin_unlock(udp_port_lock);
	}
}


static long sock_udp_bind(sock_t * sock, const struct sockaddr *addr, socklen_t addrlen) {
	if (sock->priv[0]) return -EINVAL; /* Already bound */

	/* Get port */
	const struct sockaddr_in * addr_in = (const struct sockaddr_in *)addr;
	int port = ntohs(addr_in->sin_port);
	spin_lock(udp_port_lock);
	if (hashmap_has(udp_sockets, (void*)(uintptr_t)port)) {
		spin_unlock(udp_port_lock);
		return -EADDRINUSE;
	}
	hashmap_set(udp_sockets, (void*)(uintptr_t)port, sock);
	sock->priv[0] = port;
	spin_unlock(udp_port_lock);

	/* Totally ignore the NIC stuff */

	return 0;
}

static int udp_socket(void) {
	printf("udp socket...\n");
	sock_t * sock = net_sock_create();
	sock->sock_recv = sock_udp_recv;
	sock->sock_send = sock_udp_send;
	sock->sock_close = sock_udp_close;
	sock->sock_bind = sock_udp_bind;
	return process_append_fd((process_t *)this_core->current_process, (fs_node_t *)sock);
}

static spin_lock_t tcp_port_lock = {0};
static void sock_tcp_close(sock_t * sock) {
	if (sock->priv[0]) {
		printf("tcp: removing port %d from bound map\n", sock->priv[0]);
		spin_lock(tcp_port_lock);
		hashmap_remove(tcp_sockets, (void*)(uintptr_t)sock->priv[0]);
		spin_unlock(tcp_port_lock);

		size_t total_length = sizeof(struct ipv4_packet) + sizeof(struct tcp_header);
		fs_node_t * nic = net_if_route(((struct sockaddr_in*)&sock->dest)->sin_addr.s_addr);
		if (!nic) return;

		struct ipv4_packet * response = malloc(total_length);
		response->length = htons(total_length);
		response->destination = ((struct sockaddr_in*)&sock->dest)->sin_addr.s_addr;
		response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
		response->ttl = 64;
		response->protocol = IPV4_PROT_TCP;
		sock->priv[2]++;
		response->ident = htons(sock->priv[2]);
		response->flags_fragment = htons(0x0);
		response->version_ihl = 0x45;
		response->dscp_ecn = 0;
		response->checksum = 0;
		response->checksum = htons(calculate_ipv4_checksum(response));

		/* Stick TCP header into payload */
		struct tcp_header * tcp_header = (struct tcp_header*)&response->payload;
		tcp_header->source_port = htons(sock->priv[0]);
		tcp_header->destination_port = ((struct sockaddr_in*)&sock->dest)->sin_port;
		tcp_header->seq_number = htonl(sock->priv32[0]);
		tcp_header->ack_number = htonl(sock->priv32[1]);
		tcp_header->flags = htons(TCP_FLAGS_FIN | TCP_FLAGS_ACK | 0x5000);
		tcp_header->window_size = htons(DEFAULT_TCP_WINDOW_SIZE);
		tcp_header->checksum = 0;
		tcp_header->urgent = 0;

		/* Calculate checksum */
		struct tcp_check_header check_hd = {
			.source = response->source,
			.destination = response->destination,
			.zeros = 0,
			.protocol = IPV4_PROT_TCP,
			.tcp_len = htons(sizeof(struct tcp_header)),
		};

		tcp_header->checksum = htons(calculate_tcp_checksum(&check_hd, tcp_header, tcp_header->payload, 0));
		net_ipv4_send(response,nic);
		free(response);
	}
}

static int next_tcp_port = 49152;
static int tcp_get_port(sock_t * sock) {
	spin_lock(tcp_port_lock);
	int out = next_tcp_port++;
	hashmap_set(tcp_sockets, (void*)(uintptr_t)out, sock);
	sock->priv[0] = out;
	spin_unlock(tcp_port_lock);
	return out;
}

static long sock_tcp_recv(sock_t * sock, struct msghdr * msg, int flags) {
	if (!sock->priv[0]) {
		printf("tcp: recv() but socket has no port\n");
		return -EINVAL;
	}

	if (msg->msg_iovlen > 1) {
		printf("net: todo: can't recv multiple iovs\n");
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;

	if (sock->unread) {
		if (sock->unread > msg->msg_iov[0].iov_len) {
			unsigned long out = msg->msg_iov[0].iov_len;
			sock->unread -= out;
			memcpy(msg->msg_iov[0].iov_base, sock->buf, out);
			char * x = malloc(sock->unread);
			memcpy(x, sock->buf + out, sock->unread);
			free(sock->buf);
			sock->buf = x;
			return out;
		} else {
			unsigned long out = sock->unread;
			sock->unread = 0;
			memcpy(msg->msg_iov[0].iov_base, sock->buf, out);
			free(sock->buf);
			sock->buf = NULL;
			return out;
		}
	}

	if (!sock->rx_queue->length && sock->priv[1] == 3) {
		return 0; /* EOF */
	}

	if (!sock->rx_queue->length && sock->nonblocking) return -EAGAIN;

	while (!sock->rx_queue->length) {
		int r = process_wait_nodes((process_t *)this_core->current_process, (fs_node_t*[]){(fs_node_t*)sock,NULL}, 200);
		if (r == -EINTR) return -ERESTARTSYS;
		if (!sock->rx_queue->length) {
			if (sock->priv[1] == 3) {
				/* Socket was closed while waiting */
				return 0;
			}
		}
	}

	char * packet = net_sock_get(sock);
	if (!packet) return -EINTR;
	struct ipv4_packet * data = (struct ipv4_packet*)(packet + sizeof(size_t));
	size_t packet_size = *(size_t*)packet;
	unsigned long resp = ntohs(data->length);

	if (resp != packet_size) {
		dprintf("packet size does not match: %zu %zu\n", resp, packet_size);
		resp = packet_size;
	}

	if (resp < sizeof(struct ipv4_packet) + sizeof(struct tcp_header)) {
		dprintf("Invalid receive data?\n");
		assert(0);
	}

	resp -=  sizeof(struct ipv4_packet) + sizeof(struct tcp_header);

	if (resp > (unsigned long)msg->msg_iov[0].iov_len) {
		memcpy(msg->msg_iov[0].iov_base, data->payload + sizeof(struct tcp_header),msg->msg_iov[0].iov_len);
		resp -= msg->msg_iov[0].iov_len;
		if (resp == 0xFFFFffffFFFFffff) printf("what\n");
		sock->unread = resp;
		sock->buf = malloc(resp);
		memcpy(sock->buf, data->payload + sizeof(struct tcp_header) + msg->msg_iov[0].iov_len, resp);
		free(packet);
		return msg->msg_iov[0].iov_len;
	}

	memcpy(msg->msg_iov[0].iov_base, data->payload + sizeof(struct tcp_header), resp);
	free(packet);
	return resp;
}

extern uint32_t rand(void);

static long sock_tcp_connect(sock_t * sock, const struct sockaddr *addr, socklen_t addrlen) {
	const struct sockaddr_in * dest = (const struct sockaddr_in *)addr;
	char deststr[16];
	ip_ntoa(ntohl(dest->sin_addr.s_addr), deststr);
	printf("tcp: connect requested to %s port %d\n", deststr, ntohs(dest->sin_port));

	if (sock->priv[1] != 0) {
		printf("tcp: socket is already connected?\n");
		return -EINVAL;
	}

	/* Get a port */
	tcp_get_port(sock);
	printf("tcp: connecting from ephemeral port %d\n", (int)sock->priv[0]);

	/* Mark as awaiting connection, send initial SYN */
	sock->priv[1] = 1;

	memcpy(&sock->dest, addr, addrlen);

	fs_node_t * nic = net_if_route(dest->sin_addr.s_addr);
	if (!nic) return -ENONET;

	size_t total_length = sizeof(struct ipv4_packet) + sizeof(struct tcp_header);

	struct ipv4_packet * response = malloc(total_length);
	response->length = htons(total_length);
	response->destination = dest->sin_addr.s_addr;
	response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
	response->ttl = 64;
	response->protocol = IPV4_PROT_TCP;
	sock->priv[2] = rand();
	response->ident = htons(sock->priv[2]);
	response->flags_fragment = htons(0x0);
	response->version_ihl = 0x45;
	response->dscp_ecn = 0;
	response->checksum = 0;
	response->checksum = htons(calculate_ipv4_checksum(response));

	/* Stick TCP header into payload */
	struct tcp_header * tcp_header = (struct tcp_header*)&response->payload;
	tcp_header->source_port = htons(sock->priv[0]);
	tcp_header->destination_port = dest->sin_port;
	tcp_header->seq_number = 0;
	tcp_header->ack_number = 0;
	tcp_header->flags = htons((1 << 1) | 0x5000);
	tcp_header->window_size = htons(DEFAULT_TCP_WINDOW_SIZE);
	tcp_header->checksum = 0;
	tcp_header->urgent = 0;

	/* Calculate checksum */
	struct tcp_check_header check_hd = {
		.source = response->source,
		.destination = response->destination,
		.zeros = 0,
		.protocol = IPV4_PROT_TCP,
		.tcp_len = htons(sizeof(struct tcp_header)),
	};

	tcp_header->checksum = htons(calculate_tcp_checksum(&check_hd, tcp_header, NULL, 0));

	net_ipv4_send(response,nic);

	//int _debug __attribute__((unused)) = 1;
	printf("tcp: waiting for connect to finish; queue = %ld\n", sock->rx_queue->length);

	unsigned long s, ss;
	unsigned long ns, nss;
	relative_time(1,0,&s,&ss);
	int attempts = 0;

	while (!sock->rx_queue->length) {
		int result = process_wait_nodes((process_t *)this_core->current_process, (fs_node_t*[]){(fs_node_t*)sock,NULL}, 200);
		relative_time(0,0,&ns,&nss);
		if (sock->priv[1] == 0) {
			free(response);
			return -ECONNREFUSED;
		}
		if (result != 0 && (ns > s || (ns == s && nss > ss))) {
			if (attempts++ == 3) {
				printf("tcp: connect timed out\n");
				free(response);
				return -ETIMEDOUT;
			}
			printf("tcp: retrying...\n");
			net_ipv4_send(response,nic);
			relative_time(1,0,&s,&ss);
		}
	}

	free(response);

	printf("tcp: queue should have data now (len = %lu), trying to read\n", sock->rx_queue->length);

	/* wait for signal that we connected or timed out */
	char * packet = net_sock_get(sock);
	if (!packet) return -EINTR;
	//struct ipv4_packet * data = packet + sizeof(size_t);
	printf("tcp: connect complete\n");
	free(packet);

	return 0;
}

ssize_t sock_tcp_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	printf("tcp: read into buffer of %zu bytes\n", size);
	struct iovec _iovec = {
		buffer, size
	};
	struct msghdr _header = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_iov = &_iovec,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};
	return sock_tcp_recv((sock_t*)node, &_header, 0);
}

static void delay_yield(size_t subticks) {
	unsigned long s, ss;
	relative_time(0, subticks, &s, &ss);
	sleep_until((process_t *)this_core->current_process, s, ss);
	switch_task(0);
}

static long sock_tcp_send(sock_t * sock, const struct msghdr *msg, int flags) {
	printf("tcp: send called\n");
	if (msg->msg_iovlen > 1) {
		printf("net: todo: can't send multiple iovs\n");
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;

	size_t size_into = 0;
	size_t size_remaining = msg->msg_iov[0].iov_len;

	size_t last = arch_perf_timer();
	while (size_remaining) {
		size_t size_to_send = size_remaining > 1024 ? 1024 : size_remaining;
		size_t total_length = sizeof(struct ipv4_packet) + sizeof(struct tcp_header) + size_to_send;

		fs_node_t * nic = net_if_route(((struct sockaddr_in*)&sock->dest)->sin_addr.s_addr);
		if (!nic) return -ENONET;

		struct ipv4_packet * response = malloc(total_length);
		response->length = htons(total_length);
		response->destination = ((struct sockaddr_in*)&sock->dest)->sin_addr.s_addr;
		response->source = ((struct EthernetDevice*)nic->device)->ipv4_addr;
		response->ttl = 64;
		response->protocol = IPV4_PROT_TCP;
		sock->priv[2]++;
		response->ident = htons(sock->priv[2]);
		response->flags_fragment = htons(0x0);
		response->version_ihl = 0x45;
		response->dscp_ecn = 0;
		response->checksum = 0;
		response->checksum = htons(calculate_ipv4_checksum(response));

		/* Stick TCP header into payload */
		struct tcp_header * tcp_header = (struct tcp_header*)&response->payload;
		tcp_header->source_port = htons(sock->priv[0]);
		tcp_header->destination_port = ((struct sockaddr_in*)&sock->dest)->sin_port;
		tcp_header->seq_number = htonl(sock->priv32[0]);
		tcp_header->ack_number = htonl(sock->priv32[1]);
		tcp_header->flags = htons(TCP_FLAGS_PSH | TCP_FLAGS_ACK | 0x5000);
		tcp_header->window_size = htons(DEFAULT_TCP_WINDOW_SIZE);
		tcp_header->checksum = 0;
		tcp_header->urgent = 0;

		sock->priv32[0] += size_to_send;

		/* Calculate checksum */
		struct tcp_check_header check_hd = {
			.source = response->source,
			.destination = response->destination,
			.zeros = 0,
			.protocol = IPV4_PROT_TCP,
			.tcp_len = htons(sizeof(struct tcp_header) + size_to_send),
		};

		memcpy(tcp_header->payload, (char*)msg->msg_iov[0].iov_base + size_into, size_to_send);
		tcp_header->checksum = htons(calculate_tcp_checksum(&check_hd, tcp_header, tcp_header->payload, size_to_send));
		net_ipv4_send(response,nic);
		free(response);

		size_remaining -= size_to_send;
		size_into += size_to_send;

		if (size_remaining) {
			/* Keep us away from the BSP... */
			if (processor_count > 1) {
				if (this_core->cpu_id == 0) {
					delay_yield(0);
				}
			} else {
				if (arch_perf_timer() - last > 10000UL * arch_cpu_mhz()) {
					delay_yield(0);
					last = arch_perf_timer();
				}
			}
		}
	}

	return size_into;
}

ssize_t sock_tcp_write(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	printf("tcp: write of %zu bytes\n", size);
	struct iovec _iovec = {
		(void*)buffer, size
	};
	struct msghdr _header = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_iov = &_iovec,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};
	return sock_tcp_send((sock_t*)node, &_header, 0);
}

long sock_tcp_getsockname(sock_t * sock, struct sockaddr *addr, socklen_t * addrlen) {
	in_addr_t ip4_addr = 0;
	fs_node_t * nic = net_if_route(((struct sockaddr_in*)&sock->dest)->sin_addr.s_addr);
	if (nic) {
		ip4_addr = ((struct EthernetDevice*)nic->device)->ipv4_addr;
	}

	struct sockaddr_in out = {
		AF_INET, htons(sock->priv[0]), { ip4_addr }, {0},
	};

	memcpy(addr, &out, *addrlen < sizeof(struct sockaddr_in) ? *addrlen : sizeof(struct sockaddr_in));
	if (*addrlen < sizeof(struct sockaddr_in)) *addrlen = sizeof(struct sockaddr_in);
	return 0;
}

long sock_tcp_getpeername(sock_t * sock, struct sockaddr *addr, socklen_t * addrlen) {
	in_addr_t ip4_addr = ((struct sockaddr_in*)&sock->dest)->sin_addr.s_addr;
	struct sockaddr_in out = {
		AF_INET, ((struct sockaddr_in*)&sock->dest)->sin_port, { ip4_addr }, {0},
	};
	memcpy(addr, &out, *addrlen < sizeof(struct sockaddr_in) ? *addrlen : sizeof(struct sockaddr_in));
	if (*addrlen < sizeof(struct sockaddr_in)) *addrlen = sizeof(struct sockaddr_in);
	return 0;
}

static int tcp_socket(void) {
	printf("tcp socket...\n");
	sock_t * sock = net_sock_create();
	sock->sock_recv = sock_tcp_recv;
	sock->sock_send = sock_tcp_send;
	sock->sock_close = sock_tcp_close;
	sock->sock_connect = sock_tcp_connect;
	sock->sock_getsockname = sock_tcp_getsockname;
	sock->sock_getpeername = sock_tcp_getpeername;
	sock->_fnode.read = sock_tcp_read;
	sock->_fnode.write = sock_tcp_write;
	int fd = process_append_fd((process_t *)this_core->current_process, (fs_node_t *)sock);
	FD_MODE(fd) = 03;
	return fd;
}

long net_ipv4_socket(int type, int protocol) {
	/* Ignore protocol, make socket for 'type' only... */
	switch (type) {
		case SOCK_DGRAM:
			if (!protocol || protocol == IPPROTO_UDP)
				return udp_socket();
			if (protocol == IPPROTO_ICMP)
				return icmp_socket();
			return -EINVAL;
		case SOCK_STREAM:
			return tcp_socket();
		default:
			return -EINVAL;
	}
}
