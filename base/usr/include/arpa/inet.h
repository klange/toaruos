#pragma once

#include <_cheader.h>
#include <stdint.h>
#include <sys/types.h>
#include <netinet/in.h>

_Begin_C_Header

#define INADDR_ANY (unsigned long int)0x0
#define INADDR_BROADCAST (unsigned long int)0xFFffFFffUL

#ifndef _KERNEL_

extern uint32_t htonl(uint32_t hostlong);
extern uint16_t htons(uint16_t hostshort);
extern uint32_t ntohl(uint32_t netlong);
extern uint16_t ntohs(uint16_t netshort);

in_addr_t inet_addr(const char *cp);
char *inet_ntoa(struct in_addr in);

#endif

_End_C_Header
