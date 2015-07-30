#ifndef KERNEL_MOD_NET_H
#define KERNEL_MOD_NET_H

typedef uint8_t* (*get_mac_func)(void);
typedef struct ethernet_packet* (*get_packet_func)(void);
typedef void* (*send_packet_func)(uint8_t*, size_t);

struct netif {
	void *extra;
	// void (*write_packet)(struct sized_blob * payload);
	get_mac_func get_mac;
	// struct ethernet_packet* (*get_packet)(uint8_t* payload, size_t payload_size);
	get_packet_func get_packet;
	// void (*send_packet)(uint8_t* payload, size_t payload_size);
	send_packet_func send_packet;
	uint8_t hwaddr[6];
	uint32_t source;
};

extern void init_netif_funcs(get_mac_func mac_func, get_packet_func get_func, send_packet_func send_func);
extern void net_handler(void * data, char * name);
extern size_t write_dhcp_packet(uint8_t * buffer);

#endif
