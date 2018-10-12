#pragma once

#include <_cheader.h>
#include <limits.h>

_Begin_C_Header

typedef int gid_t;
typedef int uid_t;
typedef int dev_t;
typedef int ino_t;
typedef int mode_t;
typedef int caddr_t;

typedef unsigned short nlink_t;

typedef long off_t;
typedef long time_t;

#if defined(__INT_MAX__) && __INT_MAX__ == 2147483647
typedef int ssize_t;
#else
typedef long ssize_t;
#endif

typedef unsigned long useconds_t;
typedef long suseconds_t;
typedef int pid_t;

#define FD_SETSIZE 64 /* compatibility with newlib */
typedef long fd_mask;
typedef struct _fd_set {
    fd_mask fds_bits[2]; /* should be 64 bits */
} fd_set;

_End_C_Header
