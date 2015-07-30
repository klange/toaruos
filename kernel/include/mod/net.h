#ifndef KERNEL_MOD_NET_H
#define KERNEL_MOD_NET_H

extern void net_handler(void * data, char * name);
extern size_t write_dhcp_packet(uint8_t * buffer);

#endif
