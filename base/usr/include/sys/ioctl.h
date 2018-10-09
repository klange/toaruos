#pragma once

#include <termios.h>

#define IOCTLDTYPE 0x4F00

#define IOCTL_DTYPE_UNKNOWN -1
#define IOCTL_DTYPE_FILE     1
#define IOCTL_DTYPE_TTY      2

#define IOCTLTTYNAME 0x4F01

#define IOCTL_PACKETFS_QUEUED 0x5050

