#pragma once

#include <limits.h>

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
