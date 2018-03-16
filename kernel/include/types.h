/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#pragma once

/* Types */

#define NULL ((void *)0UL)

#include "../../include/stdint.h"

typedef unsigned long size_t;
#define CHAR_BIT 8

struct timeval {
	uint32_t tv_sec;
	uint32_t tv_usec;
};

