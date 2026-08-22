#include <stdint.h>
#include <_cheader.h>

_Begin_C_Header

struct sockaddr_pex {
	unsigned short spex_family;
	char spex_target[100];
};

struct sockaddr_pex_client {
	unsigned short spexc_family;
	uintptr_t spexc_addr;
};

_End_C_Header
