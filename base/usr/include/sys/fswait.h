#pragma once

#include <_cheader.h>

_Begin_C_Header
extern int fswait(int count, int * fds);
extern int fswait2(int count, int * fds, int timeout);
_End_C_Header
