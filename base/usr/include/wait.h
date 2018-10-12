#pragma once

#include <_cheader.h>

_Begin_C_Header
int waitpid(int pid, int *status, int options);
int wait(int *status);
_End_C_Header
