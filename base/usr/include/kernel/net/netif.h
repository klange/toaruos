#pragma once

#include <kernel/vfs.h>
#include <sys/socket.h>

#define htonl(l)  ( (((l) & 0xFF) << 24) | (((l) & 0xFF00) << 8) | (((l) & 0xFF0000) >> 8) | (((l) & 0xFF000000) >> 24))
#define htons(s)  ( (((s) & 0xFF) << 8) | (((s) & 0xFF00) >> 8) )
#define ntohl(l)  htonl((l))
#define ntohs(s)  htons((s))

int net_add_interface(const char * name, fs_node_t * deviceNode);
fs_node_t * net_if_lookup(const char * name);
fs_node_t * net_if_route(uint32_t addr);

typedef struct SockData {
	fs_node_t _fnode;
	spin_lock_t alert_lock;
	spin_lock_t rx_lock;
	list_t * alert_wait;
	list_t * rx_wait;
	list_t * rx_queue;

	uint16_t priv[4];

	long (*sock_recv)(struct SockData * sock, struct msghdr * msg, int flags);
	long (*sock_send)(struct SockData * sock, const struct msghdr *msg, int flags);
	void (*sock_close)(struct SockData * sock);
	long (*sock_connect)(struct SockData * sock, const struct sockaddr *addr, socklen_t addrlen);
	long (*sock_bind)(struct SockData * sock, const struct sockaddr *addr, socklen_t addrlen);
	long (*sock_getsockname)(struct SockData * sock, struct sockaddr *addr, socklen_t *addrlen);
	long (*sock_getpeername)(struct SockData * sock, struct sockaddr *addr, socklen_t *addrlen);

	struct sockaddr dest;
	uint32_t priv32[4];

	size_t unread;
	char * buf;
	int nonblocking;
} sock_t;

void net_sock_alert(sock_t * sock);
void net_sock_add(sock_t * sock, void * frame, size_t size);
void * net_sock_get(sock_t * sock);
sock_t * net_sock_create(void);

extern long net_socket(int,int,int);
extern long net_setsockopt(int,int,int,const void*,socklen_t);
extern long net_bind(int, const struct sockaddr*, socklen_t);
extern long net_accept(int, struct sockaddr*, socklen_t*);
extern long net_listen(int,int);
extern long net_connect(int, const struct sockaddr*, socklen_t);
extern long net_getsockopt(int,int,int,void*,socklen_t*);
extern long net_recv(int,struct msghdr*,int);
extern long net_send(int, const struct msghdr*, int);
extern long net_shutdown(int, int);
extern long net_getsockname(int,struct sockaddr*,socklen_t*);
extern long net_getpeername(int,struct sockaddr*,socklen_t*);

