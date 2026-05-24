#pragma once

#define AT_NULL      0
#define AT_IGNORE    1
#define AT_EXECFD    2
#define AT_PHDR      3
#define AT_PHENT     4
#define AT_PHNUM     5
#define AT_PAGESZ    6
#define AT_BASE      7
#define AT_FLAGS     8
#define AT_ENTRY     9
#define AT_NOTELF    10
#define AT_UID       11 /* Real user ID */
#define AT_EUID      12 /* Effective user ID */
#define AT_GID       13 /* Real group ID */
#define AT_EGID      14 /* Effective group ID */
#define AT_PLATFORM  15
#define AT_HWCAP     16
#define AT_CLKTCK    17 /* Clock tick */
#define AT_FPUCW     18
#define AT_SECURE    23
#define AT_RANDOM    25
#define AT_EXECFN    31

#ifndef __kernel__
#include <_cheader.h>

_Begin_C_Header

extern unsigned long getauxval(unsigned long type);

_End_C_Header
#endif
