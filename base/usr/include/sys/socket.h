#pragma once

#include <_cheader.h>
#include <stdint.h>
#include <sys/types.h>

_Begin_C_Header

#define AF_UNSPEC 0
#define AF_INET 1
#define AF_RAW 2

#define PF_INET AF_INET

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define IPPROTO_IP  0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

#define SOL_SOCKET 0

#define SO_KEEPALIVE 1
#define SO_REUSEADDR 2
#define SO_BINDTODEVICE 3

typedef size_t socklen_t;

struct sockaddr {
	unsigned short    sa_family;    // address family, AF_xxx
	char              sa_data[14];  // 14 bytes of protocol address
};

struct addrinfo {
	int              ai_flags;
	int              ai_family;
	int              ai_socktype;
	int              ai_protocol;
	socklen_t        ai_addrlen;
	struct sockaddr *ai_addr;
	char            *ai_canonname;
	struct addrinfo *ai_next;
};

struct iovec {                    /* Scatter/gather array items */
	void  *iov_base;              /* Starting address */
	size_t iov_len;               /* Number of bytes to transfer */
};

struct msghdr {
	void         *msg_name;       /* optional address */
	socklen_t     msg_namelen;    /* size of address */
	struct iovec *msg_iov;        /* scatter/gather array */
	size_t        msg_iovlen;     /* # elements in msg_iov */
	void         *msg_control;    /* ancillary data, see below */
	size_t        msg_controllen; /* ancillary data buffer len */
	int           msg_flags;      /* flags on received message */
};

struct sockaddr_storage {
	unsigned short ss_family;
	char _ss_pad[128];
};

#ifndef _KERNEL_
extern ssize_t recv(int sockfd, void *buf, size_t len, int flags);
extern ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
extern ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);

ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);

extern int socket(int domain, int type, int protocol);

extern int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern int accept(int sockfd, struct sockaddr * addr, socklen_t * addrlen);
extern int accept4(int sockfd, struct sockaddr * addr, socklen_t * addrlen, int flags);
extern int listen(int sockfd, int backlog);
extern int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
extern int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
extern int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
extern int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

extern int connect(int sockfd, const struct sockaddr * addr, socklen_t addrlen);
extern int shutdown(int sockfd, int how);
#endif

_End_C_Header


