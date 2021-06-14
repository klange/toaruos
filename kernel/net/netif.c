/**
 * @file  kernel/net/netif.c
 * @brief Network interface manager.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */

#include <kernel/types.h>
#include <kernel/vfs.h>
#include <kernel/string.h>
#include <kernel/list.h>
#include <kernel/printf.h>
#include <kernel/spinlock.h>
#include <kernel/hashmap.h>
#include <kernel/net/netif.h>

#include <errno.h>

static hashmap_t * interfaces = NULL;
extern list_t * net_raw_sockets_list;
static fs_node_t * _if_first = NULL;

void net_install(void) {
	/* Set up virtual devices */
	map_vfs_directory("/dev/net");
	interfaces = hashmap_create(10);
	net_raw_sockets_list = list_create("raw sockets", NULL);
}

/* kinda temporary for now */
int net_add_interface(const char * name, fs_node_t * deviceNode) {
	hashmap_set(interfaces, name, deviceNode);

	char tmp[100];
	snprintf(tmp,100,"/dev/net/%s", name);
	vfs_mount(tmp, deviceNode);

	if (!_if_first) _if_first = deviceNode;

	return 0;
}

fs_node_t * net_if_lookup(const char * name) {
	return hashmap_get(interfaces, name);
}

fs_node_t * net_if_any(void) {
	return _if_first;
}
