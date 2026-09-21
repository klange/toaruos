#pragma once

#include <_cheader.h>
#include <stdio.h>
#include <sys/types.h>

_Begin_C_Header

struct passwd {
	char * pw_name;    // username
	char * pw_passwd;  // password (not meaningful)
	uid_t  pw_uid;     // user id
	gid_t  pw_gid;     // group id
	char * pw_comment; // used for decoration settings in toaruos
	char * pw_gecos;   // full name
	char * pw_dir;     // home directory
	char * pw_shell;   // shell
};

struct passwd * fgetpwent(FILE * stream);

struct passwd * getpwent(void);
void setpwent(void);
void endpwent(void);
struct passwd * getpwnam(const char * name);
struct passwd * getpwuid(uid_t uid);

#if defined(_DEFAULT_SOURCE) || defined(_TOARU_SOURCE)
int fgetpwent_r(FILE * stream, struct passwd *pwbuf, char * buf, size_t buflen, struct passwd **pwbufp);
int getpwent_r(struct passwd *pwbuf, char * buf, size_t buflen, struct passwd **pwbufp);
#endif

#if defined(_TOARU_SOURCE)
int fgetpwent_t(FILE * stream, struct passwd *pwbuf, char ** buf, size_t *buflen, struct passwd **pwbufp);
#endif

_End_C_Header
