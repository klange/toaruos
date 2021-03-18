#pragma once
/**
 * @file kuroko.h
 * @brief Top-level header with configuration macros.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#if defined(__EMSCRIPTEN__)
typedef long long krk_integer_type;
# define PRIkrk_int "%lld"
# define PRIkrk_hex "%llx"
# define parseStrInt strtoll
#elif defined(_WIN32)
typedef long long krk_integer_type;
# define PRIkrk_int "%I64d"
# define PRIkrk_hex "%I64x"
# define parseStrInt strtoll
# define ENABLE_THREADING
# else
typedef long krk_integer_type;
# define PRIkrk_int "%ld"
# define PRIkrk_hex "%lx"
# define parseStrInt strtol
# define ENABLE_THREADING
#endif

#define ENABLE_THREADING

#ifdef DEBUG
#define ENABLE_DISASSEMBLY
#define ENABLE_TRACING
#define ENABLE_SCAN_TRACING
#define ENABLE_STRESS_GC
#endif

