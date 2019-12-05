#pragma once

#include <_cheader.h>

_Begin_C_Header
extern int fswait(int count, int * fds);
extern int fswait2(int count, int * fds, int timeout);
extern int fswait3(int count, int * fds, int timeout, int * out);
_End_C_Header
