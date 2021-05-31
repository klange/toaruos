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

struct hostent * gethostbyname(const char * name) {
	/* This formerly called into the kernel network device to perform
	 * DNS lookups, but we're going to resolve directly with a UDP DNS
	 * client with timeouts and everything, right here in the libc... */
	UNIMPLEMENTED;
	return NULL;
}

