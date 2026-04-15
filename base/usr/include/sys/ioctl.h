#pragma once

#include <_cheader.h>
#include <termios.h>

#define IOCTLTTYNAME  0x4F01
#define IOCTLTTYLOGIN 0x4F02
#define IOCTLSYNC     0x4F03

#define IOCTL_PACKETFS_QUEUED 0x5050

#define FIONBIO  0x4e424c4b

_Begin_C_Header

struct __tty_name {
    unsigned long len;
    char * buf;
};

_End_C_Header
