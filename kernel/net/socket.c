/**
 * @file  kernel/net/socket.c
 * @brief Top-level socket manager.
 *
 * Provides the standard socket interface.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/list.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>

#include <kernel/net/netif.h>

#include <sys/socket.h>
#include <sys/ioctl.h>

#ifndef MISAKA_DEBUG_NET
#define printf(...)
#endif

/**
 * TODO: Should we have an interface for modules to install protocol handlers?
 *       Thinking this should work like the VFS, with method tables for different
 *       protocol handlers, but a lot of this stuff is also just generic...
 */
extern long net_ipv4_socket(int,int);

void net_sock_alert(sock_t * sock) {
	spin_lock(sock->alert_lock);
	while (sock->alert_wait->head) {
		node_t * node = list_dequeue(sock->alert_wait);
		process_t * p = node->value;
		free(node);
		spin_unlock(sock->alert_lock);
		process_alert_node(p, (fs_node_t*)sock);
		spin_lock(sock->alert_lock);
	}
	spin_unlock(sock->alert_lock);
}

void net_sock_add(sock_t * sock, void * frame, size_t size) {
	spin_lock(sock->rx_lock);
	char * bleh = malloc(size + sizeof(size_t));
	*(size_t*)bleh = size;
	memcpy(bleh + sizeof(size_t), frame, size);
	list_insert(sock->rx_queue, bleh);
	wakeup_queue(sock->rx_wait);
	net_sock_alert(sock);
	spin_unlock(sock->rx_lock);
}

void * net_sock_get(sock_t * sock) {
	while (!sock->rx_queue->length) {
		if (sleep_on(sock->rx_wait)) {
			if (!sock->rx_queue->length)
				return NULL;
		}
	}

	spin_lock(sock->rx_lock);
	node_t * n = list_dequeue(sock->rx_queue);
	void* value = n->value;
	free(n);
	spin_unlock(sock->rx_lock);

	return value;
}

int sock_generic_check(fs_node_t *node) {
	sock_t * sock = (sock_t*)node;
	if (sock->rx_queue->length) return 0;
	if (sock->unread) return 0;
	return 1;
}

int sock_generic_wait(fs_node_t *node, void * process) {
	sock_t * sock = (sock_t*)node;

	spin_lock(sock->alert_lock);
	if (!list_find(sock->alert_wait, process)) {
		list_insert(sock->alert_wait, process);
	}
	list_insert(((process_t *)process)->node_waits, sock);
	spin_unlock(sock->alert_lock);
	return 0;
}

void sock_generic_close(fs_node_t *node) {
	sock_t * sock = (sock_t*)node;
	sock->sock_close(sock);
	while (sock->rx_queue->length) {
		node_t * n = list_dequeue(sock->rx_queue);
		free(n->value);
		free(n);
	}
	printf("net: socket closed\n");
}

int sock_generic_ioctl(fs_node_t * node, unsigned long request, void * argp) {
	sock_t * sock = (sock_t*)node;
	switch (request) {
		case FIONBIO: {
			if (!mmu_validate_user_pointer(argp, sizeof(int), 0)) return -EFAULT;
			sock->nonblocking = (!!*(int*)argp);
			return 0;
		}
	}
	return -EINVAL;
}

sock_t * net_sock_create(void) {
	sock_t * sock = calloc(sizeof(struct SockData),1);
	sock->_fnode.flags = FS_SOCKET; /* uh, FS_SOCKET? */
	sock->_fnode.mask = 0600;
	sock->_fnode.device = NULL;
	sock->_fnode.selectcheck = sock_generic_check;
	sock->_fnode.selectwait = sock_generic_wait;
	sock->_fnode.close = sock_generic_close;
	sock->_fnode.ioctl = sock_generic_ioctl;
	sock->alert_wait = list_create("socket alert wait", sock);
	sock->rx_wait    = list_create("socket rx wait", sock);
	sock->rx_queue   = list_create("socket rx queue", sock);
	open_fs((fs_node_t*)sock,0);
	return sock;
}

spin_lock_t net_raw_sockets_lock = {0};
list_t * net_raw_sockets_list = NULL;

static long sock_raw_recv(sock_t * sock, struct msghdr * msg, int flags) {
	if (!sock->_fnode.device) return -EINVAL;
	if (msg->msg_iovlen > 1) {
		printf("net: todo: can't recv multiple iovs\n");
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;
	char * data = net_sock_get(sock);
	if (!data) return -EINTR;
	size_t packet_size = *(size_t*)data;
	if (msg->msg_iov[0].iov_len < packet_size) return -EINVAL;
	memcpy(msg->msg_iov[0].iov_base, data + sizeof(size_t), packet_size);
	free(data);
	return 4096;
}

static long sock_raw_send(sock_t * sock, const struct msghdr *msg, int flags) {
	if (!sock->_fnode.device) return -EINVAL;
	if (msg->msg_iovlen > 1) {
		printf("net: todo: can't send multiple iovs\n");
		return -ENOTSUP;
	}
	if (msg->msg_iovlen == 0) return 0;
	return write_fs(sock->_fnode.device, 0, msg->msg_iov[0].iov_len, msg->msg_iov[0].iov_base);
}

static void sock_raw_close(sock_t * sock) {
	spin_lock(net_raw_sockets_lock);
	list_delete(net_raw_sockets_list, list_find(net_raw_sockets_list, sock));
	spin_unlock(net_raw_sockets_lock);

	/* free stuff ? */
}

/**
 * Raw sockets
 */
long net_raw_socket(int type, int protocol) {
	if (type != SOCK_RAW) return -EINVAL;

	/* Make a new raw socket? */
	sock_t * sock = net_sock_create();
	sock->sock_recv = sock_raw_recv;
	sock->sock_send = sock_raw_send;
	sock->sock_close = sock_raw_close;

	spin_lock(net_raw_sockets_lock);
	list_insert(net_raw_sockets_list, sock);
	spin_unlock(net_raw_sockets_lock);

	int fd = process_append_fd((process_t *)this_core->current_process, (fs_node_t *)sock);
	return fd;
}

long net_socket(int domain, int type, int protocol) {
	switch (domain) {
		case AF_INET:
			return net_ipv4_socket(type, protocol);
		case AF_RAW:
			return net_raw_socket(type, protocol);
		default:
			return -EINVAL;
	}
}

long net_so_socket(struct SockData * sock, int optname, const void *optval, socklen_t optlen) {
	switch (optname) {
		case SO_BINDTODEVICE: {
			if (optlen < 1 || optlen > 32 || ((const char*)optval)[optlen-1] != 0) return -EINVAL;
			fs_node_t * netif = net_if_lookup((const char*)optval);
			if (!netif) return -ENOENT;
			sock->_fnode.device = netif;
			return 0;
		}
		default:
			return -ENOPROTOOPT;
	}
}

static inline int is_socket(int sockfd) {
	if (!FD_CHECK(sockfd)) return -EBADF;
	fs_node_t * node = FD_ENTRY(sockfd);
	if (!(node->flags & FS_SOCKET)) return -ENOTSOCK;
	return 0;
}

#define CHECK_SOCK(sockfd) do { int x = is_socket(sockfd); if (x) return x; } while (0)

#define ADDR_WR_ADDR 1
#define ADDR_WR_LEN  2
static inline int validate_addr_ptr(const struct sockaddr *addr, socklen_t * addrlen, int flags) {
	if (!mmu_validate_user_pointer(addrlen, sizeof(socklen_t), (flags & ADDR_WR_LEN) ? MMU_PTR_WRITE : 0)) return -EFAULT;
	if (!mmu_validate_user_pointer((void*)addr, *addrlen, (flags & ADDR_WR_ADDR) ? MMU_PTR_WRITE : 0)) return -EFAULT;
	return 0;
}

#define CHECK_ADDR_ADDRLEN(addr,addrlen,flags) do { int x = validate_addr_ptr(addr,addrlen,flags); if (x) return x; } while (0)

long net_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
	CHECK_SOCK(sockfd);
	PTR_VALIDATE(optval);
	sock_t * node = (sock_t*)FD_ENTRY(sockfd);
	switch (level) {
		case SOL_SOCKET:
			return net_so_socket(node,optname,optval,optlen);
		default:
			return -ENOPROTOOPT;
	}
	return -EINVAL;
}

long net_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
	CHECK_SOCK(sockfd);
	return -EINVAL;
}

long net_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	CHECK_SOCK(sockfd);
	sock_t * node = (sock_t*)FD_ENTRY(sockfd);
	if (!node->sock_bind) return -EINVAL;
	return node->sock_bind(node, addr, addrlen);
}

long net_accept(int sockfd, struct sockaddr * addr, socklen_t * addrlen) {
	CHECK_SOCK(sockfd);
	return -EINVAL;
}

long net_listen(int sockfd, int backlog) {
	CHECK_SOCK(sockfd);
	return -EINVAL;
}

long net_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	CHECK_SOCK(sockfd);
	sock_t * node = (sock_t*)FD_ENTRY(sockfd);
	if (!node->sock_connect) return -EINVAL;
	return node->sock_connect(node,addr,addrlen);
}

long net_recv(int sockfd, struct msghdr * msg, int flags) {
	CHECK_SOCK(sockfd);
	PTR_VALIDATE(msg);
	sock_t * node = (sock_t*)FD_ENTRY(sockfd);
	return node->sock_recv(node,msg,flags);
}

long net_send(int sockfd, const struct msghdr * msg, int flags) {
	CHECK_SOCK(sockfd);
	PTR_VALIDATE(msg);
	sock_t * node = (sock_t*)FD_ENTRY(sockfd);
	return node->sock_send(node,msg,flags);
}

long net_shutdown(int sockfd, int how) {
	return -EINVAL;
}

long net_getsockname(int sockfd, struct sockaddr *addr, socklen_t * addrlen) {
	CHECK_SOCK(sockfd);
	CHECK_ADDR_ADDRLEN(addr,addrlen,ADDR_WR_ADDR|ADDR_WR_LEN);
	sock_t * node = (sock_t*)FD_ENTRY(sockfd);
	if (!node->sock_getsockname) return -EINVAL;
	return node->sock_getsockname(node, addr, addrlen);
}

long net_getpeername(int sockfd, struct sockaddr *addr, socklen_t * addrlen) {
	CHECK_SOCK(sockfd);
	CHECK_ADDR_ADDRLEN(addr,addrlen,ADDR_WR_ADDR|ADDR_WR_LEN);
	sock_t * node = (sock_t*)FD_ENTRY(sockfd);
	if (!node->sock_getpeername) return -EINVAL;
	return node->sock_getpeername(node, addr, addrlen);
}
