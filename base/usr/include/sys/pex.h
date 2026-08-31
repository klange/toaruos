#include <stdint.h>
#include <_cheader.h>

_Begin_C_Header

#define PEX_SOCK_SERVER_NAME 0
#define PEX_SOCK_CLIENT_ADDR 1

struct sockaddr_pex {
	unsigned short spex_family;
	unsigned short spex_type;
	char spex_target[100];
};

struct sockaddr_pex_client {
	unsigned short spexc_family;
	unsigned short spexc_type;
	uintptr_t spexc_addr;
};

_End_C_Header
