/*
 * socket methods (mostly unimplemented)
 */
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(socket, SYS_SOCKET, int, int, int);
DEFN_SYSCALL5(setsockopt, SYS_SETSOCKOPT, int,int,int,const void*,size_t);
DEFN_SYSCALL3(bind, SYS_BIND, int,const void*,size_t);
DEFN_SYSCALL4(accept, SYS_ACCEPT, int,void*,size_t*,int);
DEFN_SYSCALL2(listen, SYS_LISTEN, int,int);
DEFN_SYSCALL3(connect, SYS_CONNECT, int,const void*,size_t);
DEFN_SYSCALL5(getsockopt, SYS_GETSOCKOPT, int,int,int,void*,size_t*);
DEFN_SYSCALL3(recv, SYS_RECV, int,void*,int);
DEFN_SYSCALL3(send, SYS_SEND, int,const void*,int);
DEFN_SYSCALL2(shutdown, SYS_SHUTDOWN, int, int);

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	__sets_errno(syscall_connect(sockfd,addr,addrlen));
}

/* All of these should just be reads. */
ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
	struct iovec _iovec = {
		buf, len
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
	return recvmsg(sockfd, &_header, flags);
}
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
	struct iovec _iovec = {
		buf, len
	};
	struct msghdr _header = {
		.msg_name = src_addr,
		.msg_namelen = addrlen ? *addrlen : 0,
		.msg_iov = &_iovec,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};
	ssize_t result = recvmsg(sockfd, &_header, flags);
	if (addrlen) *addrlen = _header.msg_namelen;
	return result;
}
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
	__sets_errno(syscall_recv(sockfd,msg,flags));
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
	struct iovec _iovec = {
		(void*)buf, len
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
	return sendmsg(sockfd, &_header, flags);
}
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) {
	struct iovec _iovec = {
		(void*)buf, len
	};
	struct msghdr _header = {
		.msg_name = (void*)dest_addr,
		.msg_namelen = addrlen,
		.msg_iov = &_iovec,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};
	return sendmsg(sockfd, &_header, flags);
}
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
	__sets_errno(syscall_send(sockfd,msg,flags));
}

int socket(int domain, int type, int protocol) {
	/* Thin wrapper around a new system call, I guess. */
	__sets_errno(syscall_socket(domain,type,protocol));
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
	__sets_errno(syscall_bind(sockfd,addr,addrlen));
}

int accept(int sockfd, struct sockaddr * addr, socklen_t * addrlen) {
	__sets_errno(syscall_accept(sockfd,addr,addrlen,0));
}

int accept4(int sockfd, struct sockaddr * addr, socklen_t * addrlen, int flags) {
	__sets_errno(syscall_accept(sockfd,addr,addrlen,flags));
}

int listen(int sockfd, int backlog) {
	__sets_errno(syscall_listen(sockfd,backlog));
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
	__sets_errno(syscall_getsockopt(sockfd,level,optname,optval,optlen));
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
	__sets_errno(syscall_setsockopt(sockfd,level,optname,optval,optlen));
}

int shutdown(int sockfd, int how) {
	__sets_errno(syscall_shutdown(sockfd,how));
}

#define UNIMPLEMENTED fprintf(stderr, "[libnetwork] Unimplemented: %s\n", __FUNCTION__)

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	UNIMPLEMENTED;
	return -1;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	UNIMPLEMENTED;
	return -1;
}

static uint32_t ip_aton(const char * in) {
	char ip[16];
	char * c = ip;
	uint32_t out[4];
	char * i;
	memcpy(ip, in, strlen(in) < 15 ? strlen(in) + 1 : 15);
	ip[15] = '\0';

	i = (char *)strchr(c, '.');
	*i = '\0';
	out[0] = atoi(c);
	c += strlen(c) + 1;

	i = (char *)strchr(c, '.');
	*i = '\0';
	out[1] = atoi(c);
	c += strlen(c) + 1;

	i = (char *)strchr(c, '.');
	*i = '\0';
	out[2] = atoi(c);
	c += strlen(c) + 1;

	out[3] = atoi(c);

	return htonl((out[0] << 24) | (out[1] << 16) | (out[2] << 8) | (out[3]));
}

static struct hostent _hostent = {0};
static uint32_t _hostent_addr = 0;
static char * _host_entry_list[1] = {0};

struct dns_packet {
	uint16_t qid;
	uint16_t flags;
	uint16_t questions;
	uint16_t answers;
	uint16_t authorities;
	uint16_t additional;
	uint8_t data[];
} __attribute__((packed)) __attribute__((aligned(2)));

struct hostent * gethostbyname(const char * name) {
	/* Try to open /etc/resolv.conf */
	FILE * resolv = fopen("/etc/resolv.conf","r");
	if (!resolv) {
		fprintf(stderr, "gethostbyname: no resolver\n");
		return NULL;
	}

	/* Try to get a udp socket */
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		fprintf(stderr, "gethostbyname: could not get a socket\n");
		return NULL;
	}

	/* Try to send something to the name server */
	char tmp[256];
	fread(tmp, 256, 1, resolv);
	if (strncmp(tmp,"nameserver ",strlen("nameserver "))) {
		fprintf(stderr, "gethostbyname: resolv doesn't look right?\n");
	}

	/* Try to convert so we can connect... */
	uint32_t ns_addr = ip_aton(tmp + strlen("nameserver "));
	fprintf(stderr, "gethostbyname: asking %#x\n", ns_addr);

	/* Form a DNS request */
	char dat[256];
	struct dns_packet * req = (struct dns_packet*)&dat;
	uint16_t qid = rand() & 0xFFFF;
	req->qid = htons(qid);
	req->flags = htons(0x0100);
	req->questions = htons(1);
	req->answers = htons(0);
	req->authorities = htons(0);
	req->additional = htons(0);

	fprintf(stderr, "gethostbyname: Frobbing bits...\n");

	/* Turn requested name into DNS request */
	ssize_t i = 0;
	const char * c = name;
	while (*c) {
		const char * n = strchr(c,'.');
		if (!n) n = c + strlen(c);
		ssize_t len = n - c;
		fprintf(stderr, "gethostbyname: segment of len %zd\n", len);
		req->data[i++] = len;
		for (; c < n; ++c, ++i) {
			req->data[i] = *c;
		}
		if (!*c) break;
		c++;
	}
	req->data[i++] = 0x00;
	req->data[i++] = 0x00;
	req->data[i++] = 0x01; /* A */
	req->data[i++] = 0x00;
	req->data[i++] = 0x01; /* IN */

	struct sockaddr_in dest;
	dest.sin_family = AF_INET;
	dest.sin_port   = htons(53);
	memcpy(&dest.sin_addr.s_addr, &ns_addr, sizeof(ns_addr));

	if (sendto(sock, &dat, sizeof(struct dns_packet) + i, 0, &dest, sizeof(struct sockaddr_in)) < 0) {
		fprintf(stderr, "gethostbyname: failed to send\n");
		return NULL;
	}

	char buf[1550];
	/* TODO timeout... */
	ssize_t len = recv(sock, buf, 1550, 0);
	if (len < 0) {
		fprintf(stderr, "gethostbyname: failed to recv\n");
		return NULL;
	}

	/* Now examine the response */
	fprintf(stderr, "gethostbyname: response from server\n");

	struct dns_packet * response = (struct dns_packet *)&buf;

	if (ntohs(response->answers) == 0) {
		fprintf(stderr, "gethostbyname: no answer\n");
		return NULL;
	}

	fprintf(stderr, "gethostbyname: got %d answers\n", ntohs(response->answers));

	/* XXX Just kinda blindly assume the answer is at the end */
	fprintf(stderr, "packet is of len %d, so answer must be %x\n", (int)len, *(uint32_t*)(buf+len-4));

	/* Get a return value */
	_hostent.h_name = name;
	_hostent.h_aliases = NULL;
	_hostent.h_addrtype = AF_INET;
	_hostent.h_length = sizeof(uint32_t);
	_hostent.h_addr_list = &_host_entry_list;
	_host_entry_list[0] = &_hostent_addr;
	_hostent_addr = *(uint32_t*)(buf+len-4);

	return &_hostent;
}

