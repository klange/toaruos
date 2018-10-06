#pragma once
#include <sys/ioctl.h>

extern int openpty(int * amaster, int * aslave, char * name, const struct termios *termp, const struct winsize * winp);
