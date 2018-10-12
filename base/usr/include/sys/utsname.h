#pragma once

#include <_cheader.h>

_Begin_C_Header
#define _UTSNAME_LENGTH 256

struct utsname {
	char  sysname[_UTSNAME_LENGTH];
	char nodename[_UTSNAME_LENGTH];
	char  release[_UTSNAME_LENGTH];
	char  version[_UTSNAME_LENGTH];
	char  machine[_UTSNAME_LENGTH];
	char domainname[_UTSNAME_LENGTH];
};


#ifndef _KERNEL_
extern int uname(struct utsname *__name);
#endif

_End_C_Header
