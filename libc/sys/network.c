/*
 * socket methods (mostly unimplemented)
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>


static struct hostent _out_host = {0};
static struct in_addr * _out_host_vector[2] = {NULL, NULL};
static struct in_addr * _out_host_aliases[1] = {NULL};
static uint32_t _out_addr;

#define UNIMPLEMENTED fprintf(stderr, "[libnetwork] Unimplemented: %s\n", __FUNCTION__)

struct hostent * gethostbyname(const char * name) {
	if (_out_host.h_name) free(_out_host.h_name);
	_out_host.h_name = strdup(name);
	int fd = open("/dev/net",O_RDONLY);
	void * args[2] = {(void *)name,&_out_addr};
	int ret = ioctl(fd, 0x5000, args);
	close(fd);
	if (ret) return NULL;

	_out_host_vector[0] = (struct in_addr *)&_out_addr;
	_out_host.h_aliases = (char **)&_out_host_aliases;
	_out_host.h_addrtype = AF_INET;
	_out_host.h_addr_list = (char**)_out_host_vector;
	_out_host.h_length = sizeof(uint32_t);
	return &_out_host;
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


