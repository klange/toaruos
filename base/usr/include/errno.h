#pragma once

#include <_cheader.h>

_Begin_C_Header

#include <bits/errno.h>

extern __const int * __errno_addr(void);
#define errno (*__errno_addr())
#define __sets_errno(...) long ret = __VA_ARGS__; if (ret < 0) { errno = -ret; ret = -1; } return ret
#define __sets_errno_type(tp,...) long ret = __VA_ARGS__; if (ret < 0) { errno = -ret; return (tp)-1; } return (tp)ret

_End_C_Header
