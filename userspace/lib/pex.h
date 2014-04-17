#ifndef _PEX_H__
#define _PEX_H__

#include <stdint.h>
#include <stdio.h>

typedef struct pex_packet {
	uintptr_t source;
	size_t      size;
	uint8_t     data[];
} pex_packet_t;
#define MAX_PACKET_SIZE 1024
#define PACKET_SIZE (sizeof(pex_packet_t) + MAX_PACKET_SIZE)

typedef struct pex_header {
	uintptr_t target;
	uint8_t data[];
} pex_header_t;

size_t pex_send(FILE * sock, unsigned int rcpt, size_t size, char * blob);
size_t pex_broadcast(FILE * sock, size_t size, char * blob);
size_t pex_listen(FILE * sock, pex_packet_t * packet);

size_t pex_reply(FILE * sock, size_t size, char * blob);
size_t pex_recv(FILE * sock, char * blob);
size_t pex_query(FILE * sock);

FILE * pex_bind(char * target);
FILE * pex_connect(char * target);

#endif
