#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "lib/testing.h"
#include "lib/pex.h"

int main(int argc, char * argv[]) {
	FILE * server = pex_bind("testex");
	FILE * client = pex_connect("testex");

	char * foo = "Hello World!";
	pex_reply(client, strlen(foo)+1, foo);

	pex_packet_t * p = calloc(PACKET_SIZE, 1);
	pex_listen(server, p);

	unsigned int client_id =  p->source;

	if (!strcmp("Hello World!", p->data)) {
		PASS("Client-server message received.");
	} else {
		FAIL("Expected message of 'Hello World!', got %s", p->data);
	}

	free(p);

	char * foo2 = "Hello everyone!\n";
	pex_broadcast(server, strlen(foo2)+1, foo2);

	char out[MAX_PACKET_SIZE];
	size_t size = pex_recv(client, out);
	if (!strcmp("Hello everyone!\n", out)) {
		PASS("Server broadcast received.");
	} else {
		FAIL("Expected message of 'Hello everyone\\n!', got %s", out);
	}

	char * foo3 = malloc(MAX_PACKET_SIZE);
	memset(foo3, 0x42, MAX_PACKET_SIZE);
	for (int i = 0; i < 3; ++i) {
		size_t size = pex_send(server, client_id, MAX_PACKET_SIZE, foo3);
		if (size != MAX_PACKET_SIZE) FAIL("Bad packet size (%d)", size);
		else PASS(".");
	}

	size_t tmp_size =  pex_send(server, client_id, MAX_PACKET_SIZE, foo3);
	if (tmp_size != -1) FAIL("Bad packet size (%d)", tmp_size);
	else PASS("Packet dropped successfully.");

	fclose(client);
	fclose(server);
	return 0;
}
