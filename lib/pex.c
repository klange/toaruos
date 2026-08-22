/**
 * @brief pex - Packet EXchange client library
 *
 * Provides a friendly interface to the "Packet Exchange"
 * functionality provided by the packetfs kernel interface.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */
#include <alloca.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/pex.h>

#include <toaru/pex.h>

size_t pex_send(FILE * sock, uintptr_t rcpt, size_t size, char * blob) {
	if (size > MAX_PACKET_SIZE) return -E2BIG;
	struct sockaddr_pex_client dest;
	dest.spexc_family = AF_PEX;
	dest.spexc_addr = rcpt;
	return sendto(fileno(sock), blob, size, 0, (struct sockaddr*)&dest, sizeof(struct sockaddr_pex_client));
}

size_t pex_broadcast(FILE * sock, size_t size, char * blob) {
	return pex_send(sock, 0, size, blob);
}

size_t pex_listen(FILE * sock, pex_packet_t * packet) {
	struct iovec _iovec = { &packet->data, 1024 };
	struct sockaddr_pex_client source;
	socklen_t source_size = sizeof(struct sockaddr_pex_client);
	struct msghdr msg = {
		&source,
		source_size,
		&_iovec,
		1,
		NULL,
		0,
		0
	};

	ssize_t len = recvmsg(fileno(sock), &msg, 0);

	if (len >= 0) {
		packet->source = source.spexc_addr;
		packet->size = len;
	}

	return len;
}

size_t pex_reply(FILE * sock, size_t size, char * blob) {
	return send(fileno(sock), blob, size, 0);
}

size_t pex_recv(FILE * sock, char * blob) {
	memset(blob, 0, MAX_PACKET_SIZE);
	return recv(fileno(sock), blob, MAX_PACKET_SIZE, 0);
}

FILE * pex_connect(char * target) {
	if (strlen(target) >= 100) {
		errno = EINVAL;
		return NULL;
	}

	int sock = socket(AF_PEX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (sock < 0) return NULL;

	struct sockaddr_pex addr;
	addr.spex_family = AF_PEX;
	memcpy(addr.spex_target, target, strlen(target) + 1);

	if (connect(sock, (struct sockaddr *)&addr, strlen(target) + 1 + offsetof(struct sockaddr_pex,spex_target)) < 0) return NULL;

	FILE * out = fdopen(sock, "r+");
	if (out) setbuf(out, NULL);
	return out;
}

FILE * pex_bind(char * target) {
	if (strlen(target) >= 100) {
		errno = EINVAL;
		return NULL;
	}

	int sock = socket(AF_PEX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (sock < 0) return NULL;

	struct sockaddr_pex addr;
	addr.spex_family = AF_PEX;
	memcpy(addr.spex_target, target, strlen(target) + 1);

	if (bind(sock, (struct sockaddr *)&addr, strlen(target) + 1 + offsetof(struct sockaddr_pex,spex_target)) < 0) return NULL;

	FILE * out = fdopen(sock, "a+");
	if (out) setbuf(out, NULL);
	return out;
}

size_t pex_query(FILE * sock) {
	return ioctl(fileno(sock), IOCTL_PACKETFS_QUEUED, NULL);
}
