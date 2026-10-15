#pragma once

#include <_cheader.h>
#include <bits/poll.h>

_Begin_C_Header

extern int poll(struct pollfd * fds, nfds_t nfds, int timeout);

_End_C_Header
