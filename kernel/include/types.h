/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef TYPES_H
#define TYPES_H

/* Types */

#define NULL ((void *)0UL)

#ifndef BOOTLOADER
typedef unsigned long uintptr_t;
#else
typedef unsigned short uintptr_t;
#endif
typedef unsigned long size_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned long long uint64_t;


#define CHAR_BIT 8
#define INT32_MAX 0x7fffffffL
#define UINT32_MAX 0xffffffffL

#endif
