#pragma once

#include <_cheader.h>

#include <bits/timeval.h>

_Begin_C_Header

typedef struct __fd_set {
	unsigned long __bits[16];
} fd_set;

#define FD_SETSIZE 1024

#define FD_ZERO(x)    (__select_fd_zero((x)))
#define FD_CLR(i,x)   (__select_fd_clear((x),(i)))
#define FD_SET(i,x)   (__select_fd_set((x),(i)))
#define FD_ISSET(i,x) (__select_fd_isset((x),(i)))

int __select_fd_zero(fd_set *);
int __select_fd_set(fd_set *, int);
int __select_fd_clear(fd_set *, int);
int __select_fd_isset(fd_set *, int);

int select(int nfds, fd_set * __restrict readfds, fd_set * __restrict writefds, fd_set * __restrict errorfds, struct timeval * __restrict timeout);

_End_C_Header
