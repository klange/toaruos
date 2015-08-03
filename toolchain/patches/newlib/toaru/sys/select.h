#ifndef _TOARU_SYS_SELECT_H
#define _TOARU_SYS_SELECT_H

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
	return 1;
}

#undef /* _TOARU_SYS_SELECT_H */
