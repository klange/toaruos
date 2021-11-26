/***
 * getpwent, setpwent, endpwent, fgetpwent
 * getpwuid, getpwnam
 *
 * These functions manage entries in the password files.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>

/*
struct passwd {
	char * pw_name;    // username
	char * pw_passwd;  // password (not meaningful)
	uid_t  pw_uid;     // user id
	gid_t  pw_gid;     // group id
	char * pw_comment; // used for decoration settings in toaruos
	char * pw_gecos;   // full name
	char * pw_dir;     // home directory
	char * pw_shell;   // shell
}
*/

static FILE * pwdb = NULL;

static void open_it(void) {
	pwdb = fopen("/etc/passwd", "r");
}

#define LINE_LEN 2048

static struct passwd * pw_ent;
static char * pw_blob;

struct passwd * fgetpwent(FILE * stream) {
	if (!stream) {
		return NULL;
	}
	if (!pw_ent) {
		pw_ent = malloc(sizeof(struct passwd));
		pw_blob = malloc(LINE_LEN);
	}

	memset(pw_blob, 0x00, LINE_LEN);
	fgets(pw_blob, LINE_LEN, stream);


	if (pw_blob[strlen(pw_blob)-1] == '\n') {
		pw_blob[strlen(pw_blob)-1] = '\0'; /* erase newline */
	}

	/* Tokenize */
	char *p, *tokens[8], *last;
	int i = 0;
	for ((p = strtok_r(pw_blob, ":", &last)); p;
			(p = strtok_r(NULL, ":", &last)), i++) {
		tokens[i] = p;
	}

	if (i < 8) return NULL;

	pw_ent->pw_name    = tokens[0];
	pw_ent->pw_passwd  = tokens[1];
	pw_ent->pw_uid     = atoi(tokens[2]);
	pw_ent->pw_gid     = atoi(tokens[3]);
	pw_ent->pw_gecos   = tokens[4];
	pw_ent->pw_dir     = tokens[5];
	pw_ent->pw_shell   = tokens[6];
	pw_ent->pw_comment = tokens[7];

	return pw_ent;
}

struct passwd * getpwent(void) {
	if (!pwdb) {
		open_it();
	}

	if (!pwdb) {
		return NULL;
	}

	return fgetpwent(pwdb);
}

void setpwent(void) {
	/* Reset stream to beginning */
	if (pwdb) rewind(pwdb);
}

void endpwent(void) {
	/* Close stream */
	if (pwdb) {
		fclose(pwdb);
		pwdb = NULL;
	}
	if (pw_ent) {
		free(pw_ent);
		free(pw_blob);
		pw_ent = NULL;
		pw_blob = NULL;
	}
}

struct passwd * getpwnam(const char * name) {
	struct passwd * p;

	setpwent();

	while ((p = getpwent())) {
		if (!strcmp(p->pw_name, name)) {
			return p;
		}
	}

	return NULL;
}

struct passwd * getpwuid(uid_t uid) {
	struct passwd * p;

	setpwent();

	while ((p = getpwent())) {
		if (p->pw_uid == uid) {
			return p;
		}
	}

	return NULL;
}


