/**
 * @file  kernel/net/unix.c
 * @brief Unix domain sockets
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
#include <kernel/net/netif.h>
#include <sys/socket.h>

void unix_sock_install(void) {
	/* TODO */
}

long net_unix_socket(int type, int protocol, int flags, int nb) {
	return -ESOCKTNOSUPPORT;
}
