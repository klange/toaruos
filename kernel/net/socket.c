/**
 * @file  kernel/net/socket.c
 * @brief Top-level socket manager.
 *
 * Provides the standard socket interface.
 *
 * @copyright This file is part of ToaruOS and is released under the terms
 *            of the NCSA / University of Illinois License - see LICENSE.md
 * @author    2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>

#include <sys/socket.h>

/**
 * TODO: Should we have an interface for modules to install protocol handlers?
 *       Thinking this should work like the VFS, with method tables for different
 *       protocol handlers, but a lot of this stuff is also just generic...
 */
extern long net_ipv4_socket(int,int);

/**
 * Raw sockets
 */
long net_raw_socket(int type, int protocol) {
	if (type != SOCK_RAW) return -EINVAL;

	/* Make a new raw socket? */
	fs_node_t * sock = calloc(sizeof(fs_node_t),1);
	sock->flags = FS_PIPE; /* uh, FS_SOCKET? */
	sock->mask = 0600;
	//sock->read = sock_raw_read;
	//sock->write = sock_raw_write;

	int fd = process_append_fd((process_t *)this_core->current_process, sock);
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

long net_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
	return -EINVAL;
}

long net_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
	return -EINVAL;
}

long net_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	return -EINVAL;
}

long net_accept(int sockfd, struct sockaddr * addr, socklen_t * addrlen) {
	return -EINVAL;
}

long net_listen(int sockd, int backlog) {
	return -EINVAL;
}

long net_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	return -EINVAL;
}

long net_recv(int sockfd, struct msghdr * msg, int flags) {
	return -EINVAL;
}

long net_send(int sockfd, const struct msghdr * msg, int flags) {
	return -EINVAL;
}

long net_shutdown(int sockfd, int how) {
	return -EINVAL;
}
