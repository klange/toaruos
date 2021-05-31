/**
 * @file  kernel/net/ipv4.c
 * @brief IPv4 protocol implementation.
 *
 * @copyright This file is part of ToaruOS and is released under the terms
 *            of the NCSA / University of Illinois License - see LICENSE.md
 * @author    2021 K. Lange
 */
#include <errno.h>
#include <kernel/types.h>
#include <kernel/printf.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>

#include <sys/socket.h>

long net_ipv4_socket(int type, int protocol) {
	/* Ignore protocol, make socket for 'type' only... */
	switch (type) {
		case SOCK_DGRAM:
			printf("udp socket...\n");
			return -EINVAL;
		case SOCK_STREAM:
			printf("tcp socket...\n");
			return -EINVAL;
		default:
			return -EINVAL;
	}
}
