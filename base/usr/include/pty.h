#pragma once

#include <_cheader.h>
#include <sys/ioctl.h>

_Begin_C_Header
extern int openpty(int * amanager, int * asubsidiary, char * name, const struct termios *termp, const struct winsize * winp);
extern pid_t forkpty(int * amanager, char * name, const struct termios *termp, const struct winsize * winp);
extern int login_tty(int fd);
_End_C_Header
