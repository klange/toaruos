#pragma once

#include <_cheader.h>
#include <sys/ioctl.h>

_Begin_C_Header
extern int openpty(int * amanager, int * asubsidiary, char * name, const struct termios *termp, const struct winsize * winp);
_End_C_Header
