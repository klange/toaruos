/**
 * @file libc/sys/select.c
 * @brief select() implemented via poll()
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <sys/select.h>

int __select_fd_zero(fd_set * s) {
	memset(s, 0, sizeof(fd_set));
	return 0;
}

int __select_fd_set(fd_set * s, int i) {
	s->__bits[i / 16] |= (1UL << (i % 64));
	return 0;
}

int __select_fd_clear(fd_set * s, int i) {
	s->__bits[i / 16] &= ~(1UL << (i % 64));
	return 0;
}

int __select_fd_isset(fd_set * s, int i) {
	return (s->__bits[i / 16] & (1UL << (i % 64))) != 0;
}

int select(int nfds, fd_set * restrict readfds, fd_set * restrict writefds, fd_set * restrict errorfds, struct timeval * restrict timeout) {
	int timeout_ms = timeout ? (timeout->tv_sec * 1000 + timeout->tv_usec / 1000) : -1;

	int count = 0;
	for (int i = 0; i < nfds; ++i) {
		if ((readfds  && FD_ISSET(i, readfds)) ||
		    (writefds && FD_ISSET(i, writefds)) ||
		    (errorfds && FD_ISSET(i, errorfds))) count++;
	}

	struct pollfd * fds = NULL;

	if (count) {
		fds = calloc(count, sizeof(struct pollfd));
		int x = 0;
		for (int i = 0; i < FD_SETSIZE; ++i) {
			if (!((readfds  && FD_ISSET(i, readfds)) ||
			      (writefds && FD_ISSET(i, writefds)) ||
			      (errorfds && FD_ISSET(i, errorfds)))) continue;

			int flags = 0;
			if (readfds  && FD_ISSET(i, readfds))  flags |= POLLIN;
			if (writefds && FD_ISSET(i, writefds)) flags |= POLLOUT;

			fds[x].fd = i;
			fds[x].events = flags;
			fds[x].revents = 0;

			x++;
		}
	}

	int r = poll(fds, count, timeout_ms);

	if (r <= 0) {
		free(fds);
		return r;
	}

	int bits = 0;

	for (int i = 0; i < count; ++i) {
		if (readfds  && FD_ISSET(i, readfds) ) {
			if (!(fds[i].revents & POLLIN)) {
				FD_CLR(i, readfds);
			} else {
				bits++;
			}
		}
		if (writefds && FD_ISSET(i, writefds)) {
			if (!(fds[i].revents & POLLOUT)) {
				FD_CLR(i, writefds);
			} else {
				bits++;
			}
		}
		if (errorfds && FD_ISSET(i, errorfds)) {
			if (!(fds[i].revents & (POLLERR | POLLHUP))) {
				FD_CLR(i, errorfds);
			} else {
				bits++;
			}
		}
	}

	free(fds);
	return bits;
}
