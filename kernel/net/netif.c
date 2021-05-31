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

#include <errno.h>

void net_install(void) {
	map_vfs_directory("/dev/net");
}
