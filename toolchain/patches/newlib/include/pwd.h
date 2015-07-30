/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 *
 * getpwent, setpwent, endpwent, fgetpwent
 * getpwuid, getpwnam
 *
 * These functions manage entries in the password files.
 */

#ifndef _PWD_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <sys/types.h>

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

#ifdef __cplusplus
}
#endif
#endif /* _PWD_H_ */
