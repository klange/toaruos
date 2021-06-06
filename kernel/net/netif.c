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

void net_install(void) {
	map_vfs_directory("/dev/net");
	interfaces = hashmap_create(10);
}

/* kinda temporary for now */
int net_add_interface(const char * name, fs_node_t * deviceNode) {
	hashmap_set(interfaces, name, deviceNode);

	char tmp[100];
	snprintf(tmp,100,"/dev/net/%s", name);
	vfs_mount(tmp, deviceNode);

	return 0;
}
