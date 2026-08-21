#pragma once
#include <_cheader.h>

_Begin_C_Header

struct sockaddr_un {
	short sun_family;
	char  sun_path[108];
};

_End_C_Header
