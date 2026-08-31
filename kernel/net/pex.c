/**
 * @file  kernel/net/pex.c
 * @brief Socket implementation of Toaru's packet exchange filesystem.
 *
 * PEX provides reliable, in-order, packet-based local communication.
 *
 * Clients are connection-mode, but the server is connectionless.
 *
 * The server acts as a datagram socket, using the sendto/sendmsg and
 * recvfrom/recvmsg interfaces. The server can broadcast to all of its
 * clients by sending to address 0.
 *
 * When a client disconnects, an empty message is sent to the server.
 * No indication is sent to the server when a connection is made, so
 * clients should send an initial message to say hello.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <bits/errno.h>
#include <kernel/types.h>
#include <kernel/printf.h>
#include <kernel/list.h>
#include <kernel/hashmap.h>
#include <kernel/procfs.h>
#include <kernel/net/netif.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/pex.h>

static hashmap_t * pex_servers;
static hashmap_t * pex_servers_ident;
static hashmap_t * pex_clients_ident;

static uint32_t pex_server_handles = 1;
static uint32_t pex_client_handles = 1;

#define PEX_PRIV32_SRC_ADDR    0
#define PEX_PRIV32_DST_ADDR    1
#define PEX_PRIV32_PID         2

#define PEX_PRIV_STATE         0
#define  PEX_STATE_NONE        0
#define  PEX_STATE_BOUND       1
#define  PEX_STATE_CONNECTED   2

static long recv_disconnected(sock_t * sock, struct msghdr * msg, int flags) { return -ENOTCONN; }
static long send_disconnected(sock_t * sock, const struct msghdr *msg, int flags) { return -ENOTCONN; }

static void procfs_net_pex_func(fs_node_t * node) {
	hashmap_foreach(iter, pex_servers) {
		char * name;
		sock_t * sock;
		hashmap_iter_get(&iter, &name, &sock);
		procfs_printf(node, "s %s %zu %d %d\n",
			name, sock->priv32[PEX_PRIV32_SRC_ADDR],
			sock->_fnode.uid,
			sock->priv32[PEX_PRIV32_PID]);
	}
	hashmap_foreach(iter, pex_clients_ident) {
		uintptr_t ident;
		sock_t * sock;
		hashmap_iter_get(&iter, &ident, &sock);
		procfs_printf(node, "c %zu %zu %d %d\n",
			ident, sock->priv32[PEX_PRIV32_DST_ADDR],
			sock->_fnode.uid,
			sock->priv32[PEX_PRIV32_PID]);
	}
}

static struct procfs_entry procfs_net_pex  = { 0, "pex",  procfs_net_pex_func,  0 };

void pex_sock_install(void) {
	pex_servers = hashmap_create(10);
	pex_servers_ident = hashmap_create_int(10);
	pex_clients_ident = hashmap_create_int(10);

	extern list_t * procfs_net_files;
	list_insert(procfs_net_files, &procfs_net_pex);
}

static void sock_pex_close(sock_t * sock) {
	if (sock->priv[PEX_PRIV_STATE] == PEX_STATE_BOUND) {
		hashmap_remove(pex_servers_ident, (void*)(uintptr_t)sock->priv32[PEX_PRIV32_SRC_ADDR]);
		hashmap_foreach(iter, pex_servers) {
			char * name;
			sock_t * tsock;
			hashmap_iter_get(&iter, &name, &tsock);
			if (tsock == sock) {
				hashmap_remove(pex_servers, name);
				break;
			}
		}
		hashmap_foreach(iter, pex_clients_ident) {
			uintptr_t ident;
			sock_t * dest_sock;
			hashmap_iter_get(&iter, &ident, &dest_sock);
			if (dest_sock->priv32[PEX_PRIV32_DST_ADDR] == sock->priv32[PEX_PRIV32_SRC_ADDR]) {
				net_sock_alert(dest_sock);
			}
		}
	} else if (sock->priv[PEX_PRIV_STATE] == PEX_STATE_CONNECTED) {
		sock_t * server = hashmap_get(pex_servers_ident, (void*)(uintptr_t)sock->priv32[PEX_PRIV32_DST_ADDR]);
		if (server) {
			uintptr_t tmp = sock->priv32[PEX_PRIV32_SRC_ADDR];
			net_sock_add(server, &tmp, sizeof(uintptr_t)); /* TODO actual control message queues */
		}
		hashmap_remove(pex_clients_ident, (void*)(uintptr_t)sock->priv32[PEX_PRIV32_SRC_ADDR]);
	}
}

static long pex_recv_client(sock_t * sock, struct msghdr * msg, int flags) {
	if (msg->msg_iovlen > 1) return -EINVAL;
	if (msg->msg_iovlen == 0) return 0;
	if (!sock->rx_queue->length && sock->nonblocking) return -EAGAIN;
	char * packet = net_sock_get(sock);
	if (!packet) return -EINTR;
	size_t size;
	memcpy(&size, packet, sizeof(size_t));

	if (msg->msg_iov[0].iov_len < size) {
		size = msg->msg_iov[0].iov_len;
		/* TODO set truncated */
	}

	memcpy(msg->msg_iov[0].iov_base, packet + sizeof(size_t), size);
	free(packet);

	return size;
}

static long pex_send_client(sock_t * sock, const struct msghdr *msg, int flags) {
	if (msg->msg_iovlen > 1) return -EINVAL;
	if (msg->msg_iovlen == 0) return 0;

	sock_t * dest = hashmap_get(pex_servers_ident, (void*)(uintptr_t)sock->priv32[PEX_PRIV32_DST_ADDR]);
	if (!dest) return -ECONNRESET;

	size_t packet_size = msg->msg_iov[0].iov_len;
	char * packet = malloc(packet_size + sizeof(uintptr_t));
	uintptr_t src_addr = sock->priv32[PEX_PRIV32_SRC_ADDR];
	memcpy(packet, &src_addr, sizeof(uintptr_t));
	memcpy(packet + sizeof(uintptr_t), msg->msg_iov[0].iov_base, packet_size);

	net_sock_add(dest, packet, packet_size + sizeof(uintptr_t));
	free(packet);
	return packet_size;
}


static long sock_pex_connect(sock_t * sock, const struct sockaddr *addr, socklen_t addrlen) {
	const struct sockaddr_pex *pex_addr = (void*)addr;
	if (sock->priv[PEX_PRIV_STATE] != PEX_STATE_NONE) return -EALREADY;
	sock_t * server = hashmap_get(pex_servers, pex_addr->spex_target);
	if (!server) return -ECONNREFUSED;
	sock->priv[PEX_PRIV_STATE]   = PEX_STATE_CONNECTED; /* connected */
	sock->priv32[PEX_PRIV32_SRC_ADDR] = pex_client_handles++;
	sock->priv32[PEX_PRIV32_DST_ADDR] = server->priv32[PEX_PRIV32_SRC_ADDR];
	sock->priv32[PEX_PRIV32_PID] = this_core->current_process->id;
	sock->sock_recv = pex_recv_client;
	sock->sock_send = pex_send_client;
	hashmap_set(pex_clients_ident, (void*)(uintptr_t)sock->priv32[PEX_PRIV32_SRC_ADDR], sock);

	return 0;
}

static long pex_recv_server(sock_t * sock, struct msghdr * msg, int flags) {
	if (msg->msg_iovlen > 1) return -EINVAL;
	if (msg->msg_iovlen == 0) return 0;
	if (!sock->rx_queue->length && sock->nonblocking) return -EAGAIN;

	char * packet = net_sock_get(sock);
	if (!packet) return -EINTR;
	size_t size;
	memcpy(&size, packet, sizeof(size_t));

	size -= sizeof(uintptr_t);

	if (msg->msg_iov[0].iov_len < size) {
		size = msg->msg_iov[0].iov_len;
		/* TODO set truncated */
	}

	memcpy(msg->msg_iov[0].iov_base, packet + sizeof(size_t) + sizeof(uintptr_t), size);

	if (msg->msg_namelen >= sizeof(struct sockaddr_pex_client)) {
		if (msg->msg_name) {
			uintptr_t src;
			memcpy(&src, packet + sizeof(size_t), sizeof(uintptr_t));
			((struct sockaddr_pex_client*)msg->msg_name)->spexc_family = AF_PEX;
			((struct sockaddr_pex_client*)msg->msg_name)->spexc_type = PEX_SOCK_CLIENT_ADDR;
			((struct sockaddr_pex_client*)msg->msg_name)->spexc_addr = src;
		}
	}

	msg->msg_namelen = sizeof(struct sockaddr_pex_client);

	free(packet);
	return size;
}

static long pex_send_server(sock_t * sock, const struct msghdr *msg, int flags) {
	if (msg->msg_iovlen > 1) return -EINVAL;
	if (msg->msg_iovlen == 0) return 0;
	if (msg->msg_namelen != sizeof(struct sockaddr_pex_client)) return -EINVAL;

	struct sockaddr_pex_client * dest = (struct sockaddr_pex_client*)msg->msg_name;
	size_t size = msg->msg_iov[0].iov_len;

	if (dest->spexc_addr == 0) {
		/* Bad hack for broadcast */
		hashmap_foreach(iter, pex_clients_ident) {
			uintptr_t ident;
			sock_t * dest_sock;
			hashmap_iter_get(&iter, &ident, &dest_sock);
			if (dest_sock->priv32[PEX_PRIV32_DST_ADDR] == sock->priv32[PEX_PRIV32_SRC_ADDR]) {
				net_sock_add(dest_sock, msg->msg_iov[0].iov_base, size);
			}
		}
	} else {
		sock_t * dest_sock = hashmap_get(pex_clients_ident, (void*)dest->spexc_addr);
		if (!dest_sock) return -ECONNRESET;
		net_sock_add(dest_sock, msg->msg_iov[0].iov_base, size);
	}

	return size;
}

static long sock_pex_bind(sock_t * sock, const struct sockaddr *addr, socklen_t addrlen) {
	const struct sockaddr_pex *pex_addr = (void*)addr;
	if (sock->priv[PEX_PRIV_STATE] != PEX_STATE_NONE) return -EALREADY;
	if (hashmap_has(pex_servers, pex_addr->spex_target)) return -EADDRINUSE;

	sock->priv[PEX_PRIV_STATE] = PEX_STATE_BOUND; /* bound */
	sock->priv32[PEX_PRIV32_SRC_ADDR] = pex_server_handles++;
	sock->priv32[PEX_PRIV32_PID] = this_core->current_process->id;
	sock->sock_recv = pex_recv_server;
	sock->sock_send = pex_send_server;

	hashmap_set(pex_servers, pex_addr->spex_target, sock);
	hashmap_set(pex_servers_ident, (void*)(uintptr_t)sock->priv32[PEX_PRIV32_SRC_ADDR], sock);

	return 0;
}

static int sock_pex_ioctl(fs_node_t * node, unsigned long request, void * argp) {
	sock_t * sock = (sock_t*)node;
	switch (request) {
		case IOCTL_PACKETFS_QUEUED:
			return sock->rx_queue->length > 0;
		default:
			return -ENOTTY;
	}
}

long net_pex_socket(int type, int protocol, int flags, int nb) {
	if (type != SOCK_DGRAM) return -EINVAL;

	sock_t * sock = net_sock_create();
	sock->sock_recv = recv_disconnected;
	sock->sock_send = send_disconnected;
	sock->sock_close   = sock_pex_close;
	sock->sock_connect = sock_pex_connect;
	sock->sock_bind    = sock_pex_bind;
	sock->_fnode.ioctl = sock_pex_ioctl;

	return process_append_fd((process_t *)this_core->current_process, (fs_node_t *)sock, flags | PROC_FD_MODE__RW);
}

