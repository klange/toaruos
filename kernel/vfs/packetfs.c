/**
 * @file kernel/vfs/packetfs.c
 * @brief Packet-based multiple-client IPC mechanism. aka PEX
 *
 * Provides a server-client packet-based IPC socket system for
 * userspace applications. Primarily used by the compositor to
 * communicate with clients.
 *
 * Care must be taken to ensure that this is backed by an atomic
 * stream; the legacy pseudo-pipe interface is used at the moment.
 *
 * @bug We leak kernel heap addresses directly to userspace as the
 *      client identifiers in PEX messages. We should probably do
 *      something else. I'm also reasonably certain a server can
 *      just throw a random address at the PEX API?
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2021 K. Lange
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/assert.h>
#include <kernel/types.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <kernel/pipe.h>
#include <kernel/spinlock.h>
#include <kernel/process.h>

extern void pipe_destroy(fs_node_t * node);

#include <sys/ioctl.h>

#define MAX_PACKET_SIZE 1024
#define debug_print(x, ...) do { if (0) {printf("packetfs.c [%s] ", #x); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct packet_manager {
	/* uh, nothing, lol */
	list_t * exchanges;
	spin_lock_t lock;
} pex_t;

typedef struct packet_exchange {
	char * name;
	char fresh;
	spin_lock_t lock;
	fs_node_t * server_pipe;
	list_t * clients;
	pex_t * parent;
} pex_ex_t;

typedef struct packet_client {
	pex_ex_t * parent;
	fs_node_t * pipe;
} pex_client_t;


typedef struct packet {
	pex_client_t * source;
	size_t      size;
	uint8_t     data[];
} packet_t;

typedef struct server_write_header {
	pex_client_t * target;
	uint8_t data[];
} header_t;

static ssize_t receive_packet(pex_ex_t * exchange, fs_node_t * socket, packet_t ** out) {
	ssize_t r;
	do {
		r = read_fs(socket, 0, sizeof(struct packet *), (uint8_t*)out);
	} while (r == 0);
	if (r < 0) return r;
	assert(r == sizeof(struct packet*));
	assert((uintptr_t)*out >= 0xFFFFff0000000000UL && (uintptr_t)*out < 0xffffff1fc0000000UL);
	return r;
}

static void send_to_server(pex_ex_t * p, pex_client_t * c, size_t size, void * data) {
	size_t p_size = size + sizeof(struct packet);
	packet_t * packet = malloc(p_size);
	if ((uintptr_t)c < 0x800000000) {
		printf("suspicious pex client received: %p\n", (char*)c);
	}

	packet->source = c;
	packet->size = size;

	if (size) {
		memcpy(packet->data, data, size);
	}

	write_fs(p->server_pipe, 0, sizeof(struct packet*), (uint8_t*)&packet);
}

static int send_to_client(pex_ex_t * p, pex_client_t * c, size_t size, void * data) {
	size_t p_size = size + sizeof(struct packet);

	/* Verify there is space on the client */
	if (pipe_unsize(c->pipe) < (int)sizeof(struct packet*)) {
		return -1;
	}

	if ((uintptr_t)c < 0x800000000) {
		printf("suspicious pex client received: %p\n", (char*)c);
	}

	packet_t * packet = malloc(p_size);

	memcpy(packet->data, data, size);
	packet->source = NULL;
	packet->size = size;

	write_fs(c->pipe, 0, sizeof(struct packet*), (uint8_t*)&packet);

	return size;
}

static pex_client_t * create_client(pex_ex_t * p) {
	pex_client_t * out = malloc(sizeof(pex_client_t));
	out->parent = p;
	out->pipe = make_pipe(4096);
	return out;
}

static ssize_t read_server(fs_node_t * node, off_t offset, size_t size, uint8_t * buffer) {
	pex_ex_t * p = (pex_ex_t *)node->device;
	debug_print(INFO, "[pex] server read(...)");

	packet_t * packet = NULL;

	ssize_t response_size = receive_packet(p, p->server_pipe, &packet);

	if (response_size < 0) return response_size;
	if (!packet) return -1;

	debug_print(INFO, "Server recevied packet of size %zu, was waiting for at most %lu", packet->size, size);

	if (packet->size + sizeof(packet_t) > size) {
		printf("pex: read in server would be incomplete\n");
		return -1;
	}

	memcpy(buffer, packet, packet->size + sizeof(packet_t));
	ssize_t out = packet->size + sizeof(packet_t);

	free(packet);
	return out;
}

static ssize_t write_server(fs_node_t * node, off_t offset, size_t size, uint8_t * buffer) {
	pex_ex_t * p = (pex_ex_t *)node->device;
	debug_print(INFO, "[pex] server write(...)");

	header_t * head = (header_t *)buffer;

	if (size - sizeof(header_t) > MAX_PACKET_SIZE) {
		printf("pex: server write is too big\n");
		return -1;
	}

	if (head->target == NULL) {
		/* Brodcast packet */
		spin_lock(p->lock);
		foreach(f, p->clients) {
			debug_print(INFO, "Sending to client %p", f->value);
			send_to_client(p, (pex_client_t *)f->value, size - sizeof(header_t), head->data);
		}
		spin_unlock(p->lock);
		debug_print(INFO, "Done broadcasting to clients.");
		return size;
	} else if (head->target->parent != p) {
		debug_print(WARNING, "[pex] Invalid packet from server? (pid=%d)", this_core->current_process->id);
		return -1;
	}

	return send_to_client(p, head->target, size - sizeof(header_t), head->data) + sizeof(header_t);
}

static int ioctl_server(fs_node_t * node, unsigned long request, void * argp) {
	pex_ex_t * p = (pex_ex_t *)node->device;

	switch (request) {
		case IOCTL_PACKETFS_QUEUED:
			return pipe_size(p->server_pipe);
		default:
			return -1;
	}
}

static ssize_t read_client(fs_node_t * node, off_t offset, size_t size, uint8_t * buffer) {
	pex_client_t * c = (pex_client_t *)node->inode;
	if (c->parent != node->device) {
		printf("pex: Invalid device endpoint on client read?\n");
		return -EINVAL;
	}

	debug_print(INFO, "[pex] client read(...)");

	packet_t * packet = NULL;

	ssize_t response_size = receive_packet(c->parent, c->pipe, &packet);

	if (response_size < 0) return response_size;
	if (!packet) return -EIO;

	if (packet->size > size) {
		printf("pex: Client is not reading enough bytes to hold packet of size %zu\n", packet->size);
		return -EINVAL;
	}

	memcpy(buffer, &packet->data, packet->size);
	ssize_t out = packet->size;

	debug_print(INFO, "[pex] Client received packet of size %zu", packet->size);
	if (out == 0) {
		printf("pex: packet is empty?\n");
	}

	free(packet);
	return out;
}

static ssize_t write_client(fs_node_t * node, off_t offset, size_t size, uint8_t * buffer) {
	pex_client_t * c = (pex_client_t *)node->inode;
	if (c->parent != node->device) {
		debug_print(WARNING, "[pex] Invalid device endpoint on client write?");
		return -EINVAL;
	}

	debug_print(INFO, "[pex] client write(...)");

	if (size > MAX_PACKET_SIZE) {
		debug_print(WARNING, "Size of %lu is too big.", size);
		return -EINVAL;
	}

	debug_print(INFO, "Sending packet of size %lu to parent", size);
	send_to_server(c->parent, c, size, buffer);

	return size;
}

static int ioctl_client(fs_node_t * node, unsigned long request, void * argp) {
	pex_client_t * c = (pex_client_t *)node->inode;

	switch (request) {
		case IOCTL_PACKETFS_QUEUED:
			return pipe_size(c->pipe);
		default:
			return -1;
	}
}

static void close_client(fs_node_t * node) {
	pex_client_t * c = (pex_client_t *)node->inode;
	pex_ex_t * p = c->parent;

	if (p) {
		debug_print(WARNING, "Closing packetfs client: %p:%p", (void*)p, (void*)c);
		spin_lock(p->lock);
		node_t * n = list_find(p->clients, c);
		if (n && n->owner == p->clients) {
			list_delete(p->clients, n);
			free(n);
		}
		spin_unlock(p->lock);
		char tmp[1];
		send_to_server(p, c, 0, tmp);
	}

	pipe_destroy(c->pipe);
	free(c->pipe);
	free(c);
}

static int wait_server(fs_node_t * node, void * process) {
	pex_ex_t * p = (pex_ex_t *)node->device;
	return selectwait_fs(p->server_pipe, process);
}
static int check_server(fs_node_t * node) {
	pex_ex_t * p = (pex_ex_t *)node->device;
	return selectcheck_fs(p->server_pipe);
}

static int wait_client(fs_node_t * node, void * process) {
	pex_client_t * c = (pex_client_t *)node->inode;
	return selectwait_fs(c->pipe, process);
}
static int check_client(fs_node_t * node) {
	pex_client_t * c = (pex_client_t *)node->inode;
	return selectcheck_fs(c->pipe);
}


static void close_server(fs_node_t * node) {
	pex_ex_t * ex = (pex_ex_t *)node->device;
	pex_t * p = ex->parent;

	spin_lock(p->lock);

	node_t * lnode = list_find(p->exchanges, ex);

	/* Remove from exchange list */
	if (lnode) {
		list_delete(p->exchanges, lnode);
		free(lnode);
	}

	/* Tell all clients we have disconnected */
	spin_lock(ex->lock);
	while (ex->clients->length) {
		node_t * f = list_pop(ex->clients);
		pex_client_t * client = (pex_client_t*)f->value;
		send_to_client(ex, client, 0, NULL);
		client->parent = NULL;
		free(f);
	}
	spin_unlock(ex->lock);

	free(ex->clients);
	pipe_destroy(ex->server_pipe);
	free(ex->server_pipe);
	node->device = NULL;
	free(ex);

	spin_unlock(p->lock);

}

static void open_pex(fs_node_t * node, unsigned int flags) {
	pex_ex_t * t = (pex_ex_t *)(node->device);

	debug_print(NOTICE, "Opening packet exchange %s with flags 0x%x", t->name, flags);

	if ((flags & O_CREAT) && (flags & O_EXCL)) {
		if (!t->fresh) {
			return; /* Address in use; this should be handled by kopen... */
		}
		t->fresh = 0;
		node->inode = 0;
		/* Set up the server side */
		node->read   = read_server;
		node->write  = write_server;
		node->ioctl  = ioctl_server;
		node->close  = close_server;
		node->selectcheck = check_server;
		node->selectwait  = wait_server;
		debug_print(INFO, "[pex] Server launched: %s", t->name);
		debug_print(INFO, "fs_node = %p", (void*)node);
	} else {
		pex_client_t * client = create_client(t);
		node->inode = (uintptr_t)client;

		node->read  = read_client;
		node->write = write_client;
		node->ioctl = ioctl_client;
		node->close = close_client;

		node->selectcheck = check_client;
		node->selectwait  = wait_client;

		list_insert(t->clients, client);

		/* XXX: Send plumbing message to server for new client connection */
		debug_print(INFO, "[pex] Client connected: %s:%lx", t->name, node->inode);
	}

	return;
}

static struct dirent * readdir_packetfs(fs_node_t *node, uint64_t index) {
	pex_t * p = (pex_t *)node->device;
	unsigned int i = 0;

	debug_print(INFO, "[pex] readdir(%lu)", index);

	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, "..");
		return out;
	}

	index -= 2;

	if (index >= p->exchanges->length) {
		return NULL;
	}

	spin_lock(p->lock);

	foreach(f, p->exchanges) {
		if (i == index) {
			spin_unlock(p->lock);
			pex_ex_t * t = (pex_ex_t *)f->value;
			struct dirent * out = malloc(sizeof(struct dirent));
			memset(out, 0x00, sizeof(struct dirent));
			out->d_ino = (uint64_t)t;
			strcpy(out->d_name, t->name);
			return out;
		} else {
			++i;
		}
	}

	spin_unlock(p->lock);

	return NULL;
}

static fs_node_t * file_from_pex(pex_ex_t * pex) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, pex->name);
	fnode->device  = pex;
	fnode->mask    = 0666;
	fnode->flags   = FS_CHARDEVICE;
	fnode->open    = open_pex;
	return fnode;
}

static fs_node_t * finddir_packetfs(fs_node_t * node, char * name) {
	if (!name) return NULL;
	pex_t * p = (pex_t *)node->device;

	debug_print(INFO, "[pex] finddir(%s)", name);

	spin_lock(p->lock);

	foreach(f, p->exchanges) {
		pex_ex_t * t = (pex_ex_t *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(p->lock);
			return file_from_pex(t);
		}
	}

	spin_unlock(p->lock);

	return NULL;
}

static int create_packetfs(fs_node_t *parent, char *name, mode_t permission) {
	if (!name) return -EINVAL;

	pex_t * p = (pex_t *)parent->device;

	debug_print(NOTICE, "[pex] create(%s)", name);

	spin_lock(p->lock);

	foreach(f, p->exchanges) {
		pex_ex_t * t = (pex_ex_t *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(p->lock);
			/* Already exists */
			return -EEXIST;
		}
	}

	/* Make it */
	pex_ex_t * new_exchange = malloc(sizeof(pex_ex_t));

	new_exchange->name = strdup(name);
	new_exchange->fresh = 1;
	new_exchange->clients = list_create("pex clients",new_exchange);
	new_exchange->server_pipe = make_pipe(4096);
	new_exchange->parent = p;

	spin_init(new_exchange->lock);
	/* XXX Create exchange server pipe */

	list_insert(p->exchanges, new_exchange);

	spin_unlock(p->lock);

	return 0;
}

static void destroy_pex(pex_ex_t * p) {
	/* XXX */
}

static int unlink_packetfs(fs_node_t *parent, char *name) {
	if (!name) return -EINVAL;

	pex_t * p = (pex_t *)parent->device;

	debug_print(NOTICE, "[pex] unlink(%s)", name);

	int i = -1, j = 0;

	spin_lock(p->lock);

	foreach(f, p->exchanges) {
		pex_ex_t * t = (pex_ex_t *)f->value;
		if (!strcmp(name, t->name)) {
			destroy_pex(t);
			i = j;
			break;
		}
		j++;
	}

	if (i >= 0) {
		list_remove(p->exchanges, i);
	} else {
		spin_unlock(p->lock);
		return -ENOENT;
	}

	spin_unlock(p->lock);

	return 0;
}

static fs_node_t * packetfs_manager(void) {
	pex_t * pex = malloc(sizeof(pex_t));
	pex->exchanges = list_create("pex exchanges",pex);

	spin_init(pex->lock);

	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "pex");
	fnode->device  = pex;
	fnode->mask    = 0777;
	fnode->flags   = FS_DIRECTORY;
	fnode->readdir = readdir_packetfs;
	fnode->finddir = finddir_packetfs;
	fnode->create  = create_packetfs;
	fnode->unlink  = unlink_packetfs;

	return fnode;
}

void packetfs_initialize(void) {
	fs_node_t * packet_mgr = packetfs_manager();
	vfs_mount("/dev/pex", packet_mgr);
}
