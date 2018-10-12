#pragma once

#include <_cheader.h>

_Begin_C_Header

#ifdef __GNUC__
#define alloca(size) __builtin_alloca(size)
#else
#error alloca requested but this isn't gcc
#endif

_End_C_Header
