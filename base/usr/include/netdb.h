#pragma once

#include <_cheader.h>

#include <sys/socket.h>

_Begin_C_Header

extern int getnameinfo(const struct sockaddr *addr, socklen_t addrlen,
                       char *host, socklen_t hostlen,
                       char *serv, socklen_t servlen, int flags);

extern int getaddrinfo(const char *node, const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res);

extern void freeaddrinfo(struct addrinfo *res);

#define NI_NUMERICHOST 1
#define NI_MAXHOST     255


_End_C_Header
