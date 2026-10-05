#pragma once

#include <_cheader.h>
#include <stdio.h>
#include <sys/types.h>

_Begin_C_Header

struct group {
	char * gr_name;
	char * gr_passwd;
	gid_t  gr_gid;
	char **gr_mem;
};

void setgrent(void);
void endgrent(void);
struct group * getgrent(void);

struct group *getgrnam(const char *name);
struct group *getgrgid(gid_t gid);

#if defined(_DEFAULT_SOURCE) || defined(_TOARU_SOURCE)
struct group *fgetgrent(FILE *stream);
#endif

#if defined(_TOARU_SOURCE)
int fgetgrent_t(FILE * stream, struct group *grpbuf, char ** _buf, size_t *_buflen, struct group **grpbufp);
#endif

_End_C_Header
