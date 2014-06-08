/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
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

	size_t query_result;

	query_result = pex_query(server);
	if (query_result < 1) {
		FAIL("Expected pex_query to return something > 0, got %d", query_result);
	} else {
		PASS(".");
	}

	pex_packet_t * p = calloc(PACKET_SIZE, 1);
	pex_listen(server, p);

	unsigned int client_id =  p->source;

	if (!strcmp("Hello World!", p->data)) {
		PASS("Client-server message received.");
	} else {
		FAIL("Expected message of 'Hello World!', got %s", p->data);
	}

	free(p);

	query_result = pex_query(server);
	if (query_result != 0) {
		FAIL("Expected pex_query to return 0, got %d", query_result);
	} else {
		PASS(".");
	}

	query_result = pex_query(client);
	if (query_result != 0) {
		FAIL("Expected pex_query to return 0, got %d", query_result);
	} else {
		PASS(".");
	}

	char * foo2 = "Hello everyone!\n";
	pex_broadcast(server, strlen(foo2)+1, foo2);

	query_result = pex_query(client);
	if (query_result < 1) {
		FAIL("Expected pex_query to return something > 0, got %d", query_result);
	} else {
		PASS(".");
	}

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
