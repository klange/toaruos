#pragma once
#include <_cheader.h>
#include <stdint.h>

_Begin_C_Header

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

struct in_addr {
	in_addr_t s_addr;
};

struct sockaddr_in {
	short            sin_family;   // e.g. AF_INET, AF_INET6
	unsigned short   sin_port;     // e.g. htons(3490)
	struct in_addr   sin_addr;     // see struct in_addr, below
	char             sin_zero[8];  // zero this if you want to
};

in_addr_t inet_addr(const char *cp);
char *inet_ntoa(struct in_addr in);

_End_C_Header
