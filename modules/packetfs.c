/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <fs.h>
#include <pipe.h>
#include <module.h>
#include <logging.h>
#include <ioctl.h>

#define MAX_PACKET_SIZE 1024

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

static void receive_packet(fs_node_t * socket, packet_t ** out) {
	packet_t tmp;
	read_fs(socket, 0, sizeof(struct packet), (uint8_t *)&tmp);
	*out = malloc(tmp.size + sizeof(struct packet));
	memcpy(*out, &tmp, sizeof(struct packet));

	if (tmp.size) {
		read_fs(socket, 0, tmp.size, (uint8_t *)(*out)->data);
	}
}

static void send_to_server(pex_ex_t * p, pex_client_t * c, size_t size, void * data) {
	size_t p_size = size + sizeof(struct packet);
	packet_t * packet = malloc(p_size);

	packet->source = c;
	packet->size = size;

	if (size) {
		memcpy(packet->data, data, size);
	}

	write_fs(p->server_pipe, 0, p_size, (uint8_t *)packet);

	free(packet);
}

static int send_to_client(pex_ex_t * p, pex_client_t * c, size_t size, void * data) {
	size_t p_size = size + sizeof(struct packet);

	/* Verify there is space on the client */
	if (pipe_unsize(c->pipe) < (int)p_size) {
		return -1;
	}

	packet_t * packet = malloc(p_size);

	memcpy(packet->data, data, size);
	packet->source = NULL;
	packet->size = size;

	write_fs(c->pipe, 0, p_size, (uint8_t *)packet);

	free(packet);
	return size;
}

static pex_client_t * create_client(pex_ex_t * p) {
	pex_client_t * out = malloc(sizeof(pex_client_t));
	out->parent = p;
	out->pipe = make_pipe(4096);
	return out;
}

static uint32_t read_server(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t * buffer) {
	pex_ex_t * p = (pex_ex_t *)node->device;
	debug_print(INFO, "[pex] server read(...)");

	packet_t * packet;

	receive_packet(p->server_pipe, &packet);

	debug_print(INFO, "Server recevied packet of size %d, was waiting for at most %d", packet->size, size);

	if (packet->size + sizeof(packet_t) > size) {
		return -1;
	}

	memcpy(buffer, packet, packet->size + sizeof(packet_t));
	uint32_t out = packet->size + sizeof(packet_t);

	free(packet);
	return out;
}

static uint32_t write_server(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t * buffer) {
	pex_ex_t * p = (pex_ex_t *)node->device;
	debug_print(INFO, "[pex] server write(...)");

	header_t * head = (header_t *)buffer;

	if (size - sizeof(header_t) > MAX_PACKET_SIZE) {
		return -1;
	}

	if (head->target == NULL) {
		/* Brodcast packet */
		spin_lock(p->lock);
		foreach(f, p->clients) {
			debug_print(INFO, "Sending to client 0x%x", f->value);
			send_to_client(p, (pex_client_t *)f->value, size - sizeof(header_t), head->data);
		}
		spin_unlock(p->lock);
		debug_print(INFO, "Done broadcasting to clients.");
		return size;
	} else if (head->target->parent != p) {
		debug_print(WARNING, "[pex] Invalid packet from server?");
		return -1;
	}

	return send_to_client(p, head->target, size - sizeof(header_t), head->data);
}

static int ioctl_server(fs_node_t * node, int request, void * argp) {
	pex_ex_t * p = (pex_ex_t *)node->device;

	switch (request) {
		case IOCTL_PACKETFS_QUEUED:
			return pipe_size(p->server_pipe);
		default:
			return -1;
	}
}

static uint32_t read_client(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t * buffer) {
	pex_client_t * c = (pex_client_t *)node->inode;
	if (c->parent != node->device) {
		debug_print(WARNING, "[pex] Invalid device endpoint on client read?");
		return -1;
	}

	debug_print(INFO, "[pex] client read(...)");

	packet_t * packet;

	receive_packet(c->pipe, &packet);

	if (packet->size > size) {
		debug_print(WARNING, "[pex] Client is not reading enough bytes to hold packet of size %d", packet->size);
		return -1;
	}

	memcpy(buffer, &packet->data, packet->size);
	uint32_t out = packet->size;

	debug_print(INFO, "[pex] Client received packet of size %d", packet->size);

	free(packet);
	return out;
}

static uint32_t write_client(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t * buffer) {
	pex_client_t * c = (pex_client_t *)node->inode;
	if (c->parent != node->device) {
		debug_print(WARNING, "[pex] Invalid device endpoint on client write?");
		return -1;
	}

	debug_print(INFO, "[pex] client write(...)");

	if (size > MAX_PACKET_SIZE) {
		debug_print(WARNING, "Size of %d is too big.", size);
		return -1;
	}

	debug_print(INFO, "Sending packet of size %d to parent", size);
	send_to_server(c->parent, c, size, buffer);

	return size;
}

static int ioctl_client(fs_node_t * node, int request, void * argp) {
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

	debug_print(WARNING, "Closing packetfs client: 0x%x:0x%x", p, c);

	spin_lock(p->lock);

	node_t * n = list_find(p->clients, c);
	if (n && n->owner == p->clients) {
		list_delete(p->clients, n);
		free(n);
	}

	spin_unlock(p->lock);

	char tmp[1];

	send_to_server(p, c, 0, tmp);

	free(c);
}

static void open_pex(fs_node_t * node, unsigned int flags) {
	pex_ex_t * t = (pex_ex_t *)(node->device);

	debug_print(NOTICE, "Opening packet exchange %s with flags 0x%x", t->name, flags);

	if (flags & O_CREAT && t->fresh) {
		t->fresh = 0;
		node->inode = 0;
		/* Set up the server side */
		node->read   = read_server;
		node->write  = write_server;
		node->ioctl  = ioctl_server;
		debug_print(INFO, "[pex] Server launched: %s", t->name);
		debug_print(INFO, "fs_node = 0x%x", node);
	} else if (!(flags & O_CREAT)) {
		pex_client_t * client = create_client(t);
		node->inode = (uintptr_t)client;

		node->read  = read_client;
		node->write = write_client;
		node->ioctl = ioctl_client;
		node->close = close_client;

		list_insert(t->clients, client);

		/* XXX: Send plumbing message to server for new client connection */
		debug_print(INFO, "[pex] Client connected: %s:0%x", t->name, node->inode);
	} else if (flags & O_CREAT && !t->fresh) {
		/* XXX: You dun goofed */
	}

	return;
}


static struct dirent * readdir_packetfs(fs_node_t *node, uint32_t index) {
	pex_t * p = (pex_t *)node->device;
	unsigned int i = 0;

	debug_print(INFO, "[pex] readdir(%d)", index);

	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, "..");
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
			out->ino = (uint32_t)t;
			strcpy(out->name, t->name);
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
	fnode->flags   = FS_CHARDEVICE;
	fnode->open    = open_pex;
	fnode->read    = read_server;
	fnode->write   = write_server;
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

static void create_packetfs(fs_node_t *parent, char *name, uint16_t permission) {
	if (!name) return;

	pex_t * p = (pex_t *)parent->device;

	debug_print(NOTICE, "[pex] create(%s)", name);

	spin_lock(p->lock);

	foreach(f, p->exchanges) {
		pex_ex_t * t = (pex_ex_t *)f->value;
		if (!strcmp(name, t->name)) {
			spin_unlock(p->lock);
			/* Already exists */
			return;
		}
	}

	/* Make it */
	pex_ex_t * new_exchange = malloc(sizeof(pex_ex_t));

	new_exchange->name = strdup(name);
	new_exchange->fresh = 1;
	new_exchange->clients = list_create();
	new_exchange->server_pipe = make_pipe(4096);

	spin_init(new_exchange->lock);
	/* XXX Create exchange server pipe */

	list_insert(p->exchanges, new_exchange);

	spin_unlock(p->lock);

}

static void destroy_pex(pex_ex_t * p) {
	/* XXX */
}

static void unlink_packetfs(fs_node_t *parent, char *name) {
	if (!name) return;

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
	}

	spin_unlock(p->lock);
}

static fs_node_t * packetfs_manager(void) {
	pex_t * pex = malloc(sizeof(pex_t));
	pex->exchanges = list_create();

	spin_init(pex->lock);

	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "pex");
	fnode->device  = pex;
	fnode->flags   = FS_DIRECTORY;
	fnode->readdir = readdir_packetfs;
	fnode->finddir = finddir_packetfs;
	fnode->create  = create_packetfs;
	fnode->unlink  = unlink_packetfs;

	return fnode;
}

static int init(void) {
	fs_node_t * packet_mgr = packetfs_manager();
	vfs_mount("/dev/pex", packet_mgr);
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(packetfs, init, fini);
