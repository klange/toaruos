#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef struct packet {
	uintptr_t source;
	size_t      size;
	uint8_t     data[];
} packet_t;
#define MAX_PACKET_SIZE 1024
#define PACKET_SIZE (sizeof(packet_t) + MAX_PACKET_SIZE)

typedef struct server_write_header {
	uintptr_t target;
	uint8_t data[];
} header_t;

int main(int argc, char * argv[]) {
	FILE * server = fopen("/dev/pex/testex", "a+");
	FILE * client = fopen("/dev/pex/testex", "r+");

	/* Output is unbuffered so we can write packets discreetly */
	setbuf(server, NULL);
	setbuf(client, NULL);

	fprintf(stderr, "[server = %p]\n", server);
	fprintf(stderr, "[client = %p]\n", client);

	fprintf(stderr, "Client is sending 'Hello World'.\n");
	fprintf(client, "Hello World!");

	{
		packet_t * p = malloc(PACKET_SIZE);
		memset(p, 0x00, PACKET_SIZE);
		fprintf(stderr, "Server is reading up to %d...\n", PACKET_SIZE);
		size_t size = read(fileno(server), p, PACKET_SIZE);

		fprintf(stderr, "Server received a packet of size %d (%d) from client [0x%x]\n", p->size, size, p->source);
		fprintf(stderr, "Packet contents: %s\n", p->data);
		free(p);
	}

	{
		header_t * broadcast = malloc(sizeof(header_t) + MAX_PACKET_SIZE);

		broadcast->target = 0;
		fprintf(stderr, "Server is sending broadcast response: 'Hello everyone!\\n'\n");
		size_t size = sprintf(broadcast->data, "Hello everyone!\n") + 1;

		fwrite(broadcast, 1, sizeof(header_t) + size, server);
		free(broadcast);
	}

	char out[MAX_PACKET_SIZE];
	memset(out, 0, MAX_PACKET_SIZE);
	fprintf(stderr, "Client is reading up to %d...\n", MAX_PACKET_SIZE);
	size_t size = read(fileno(client), out, MAX_PACKET_SIZE);
	fprintf(stderr, "Client received response from server (size=%d) %s\n", size, out);


	fclose(client);
	fclose(server);
	return 0;
}
