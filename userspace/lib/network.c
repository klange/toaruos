/*
 * libnetwork
 *
 * Install me to /usr/lib/libnetwork.a
 */
#include <stdio.h>
#include <stdint.h>

#include "network.h"

static struct hostent _out_host;

#define UNIMPLEMENTED fprintf(stderr, "[libnetwork] Unimplemented: %s\n", __FUNCTION__)

struct hostent * gethostbyname(const char * name) {
	UNIMPLEMENTED;
	return NULL;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	UNIMPLEMENTED;
	return -1;
}

/* All of these should just be reads. */
ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
	UNIMPLEMENTED;
	return -1;
}
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
	UNIMPLEMENTED;
	return -1;
}
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
	UNIMPLEMENTED;
	return -1;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
	UNIMPLEMENTED;
	return len;
}
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) {
	UNIMPLEMENTED;
	return len;
}
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
	UNIMPLEMENTED;
	return -1;
}

int socket(int domain, int type, int protocol) {
	UNIMPLEMENTED;
	return -1;
}

uint32_t htonl(uint32_t hostlong) {
	return ( (((hostlong) & 0xFF) << 24) | (((hostlong) & 0xFF00) << 8) | (((hostlong) & 0xFF0000) >> 8) | (((hostlong) & 0xFF000000) >> 24));
}

uint16_t htons(uint16_t hostshort) {
	return ( (((hostshort) & 0xFF) << 8) | (((hostshort) & 0xFF00) >> 8) );
}

uint32_t ntohl(uint32_t netlong) {
	return htonl(netlong);
}

uint16_t ntohs(uint16_t netshort) {
	return htons(netshort);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	UNIMPLEMENTED;
	return -1;
}

int accept(int sockfd, struct sockaddr * addr, socklen_t * addrlen) {
	UNIMPLEMENTED;
	return -1;
}

int listen(int sockfd, int backlog) {
	UNIMPLEMENTED;
	return -1;
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	UNIMPLEMENTED;
	return -1;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	return -1;
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
	UNIMPLEMENTED;
	return -1;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
	UNIMPLEMENTED;
	return -1;
}

