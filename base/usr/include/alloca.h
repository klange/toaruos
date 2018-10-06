#pragma once

#ifdef __GNUC__
#define alloca(size) __builtin_alloca(size)
#else
#error alloca requested but this isn't gcc
#endif

