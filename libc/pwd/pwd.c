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
#define _TOARU_SOURCE
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
	pwdb = fopen("/etc/passwd", "re");
}

#define LINE_LEN 2048

static struct passwd pw_ent;
static char * pw_blob = NULL;
static size_t pw_blob_avail = 0;

static void pwbuf_common(struct passwd *pwbuf, char *tokens[]) {
	pwbuf->pw_name    = tokens[0];
	pwbuf->pw_passwd  = tokens[1];
	pwbuf->pw_uid     = atoi(tokens[2]);
	pwbuf->pw_gid     = atoi(tokens[3]);
	pwbuf->pw_gecos   = tokens[4];
	pwbuf->pw_dir     = tokens[5];
	pwbuf->pw_shell   = tokens[6];
	pwbuf->pw_comment = tokens[7];
}

/*
 * Get an entry from a password database file, but with a getline-style interface.
 */
int fgetpwent_t(FILE * stream, struct passwd *pwbuf, char ** _buf, size_t *_buflen, struct passwd **pwbufp) {
	ssize_t len;
	if ((len = getline(_buf, _buflen, stream)) <= 0) {
		*pwbufp = NULL;
		return ENOENT;
	}

	char * buf = *_buf;
	if (buf[len-1] == '\n') buf[len-1] = '\0';

	char *p, *tokens[8], *last;
	int i = 0;
	for ((p = strtok_r(buf, ":", &last)); p;
			(p = strtok_r(NULL, ":", &last)), i++) {
		tokens[i] = p;
	}

	if (i < 8) {
		*pwbufp = NULL;
		return ENOENT;
	}

	pwbuf_common(pwbuf, tokens);

	*pwbufp = pwbuf;
	return 0;
}

/*
 * BSD/GNU reentrant form
 */
int fgetpwent_r(FILE * stream, struct passwd *pwbuf, char * buf, size_t buflen, struct passwd **pwbufp) {
	char * pw_blob = NULL;
	size_t pw_blob_avail = 0;
	ssize_t len;

	fpos_t before;
	fgetpos(stream, &before);

	if ((len = getline(&pw_blob, &pw_blob_avail, stream)) <= 0) {
		free(pw_blob);
		*pwbufp = NULL;
		return ENOENT;
	}

	if (buflen < (size_t)len + 1) {
		fsetpos(stream, &before);
		free(pw_blob);
		*pwbufp = NULL;
		return ERANGE;
	}

	memcpy(buf, pw_blob, len + 1);
	free(pw_blob);

	if (buf[len-1] == '\n') buf[len-1] = '\0';

	char *p, *tokens[8], *last;
	int i = 0;
	for ((p = strtok_r(buf, ":", &last)); p;
			(p = strtok_r(NULL, ":", &last)), i++) {
		tokens[i] = p;
	}

	if (i < 8) {
		*pwbufp = NULL;
		return ENOENT;
	}

	pwbuf_common(pwbuf, tokens);

	*pwbufp = pwbuf;
	return 0;
}

int getpwent_r(struct passwd *pwbuf, char * buf, size_t buflen, struct passwd **pwbufp) {
	if (!pwdb) open_it();
	if (!pwdb) {
		*pwbufp = NULL;
		return ENOENT;
	}
	return fgetpwent_r(pwdb, pwbuf, buf, buflen, pwbufp);
}

struct passwd * fgetpwent(FILE * stream) {
	size_t len;
	if (!stream) return NULL;
	if ((len = getline(&pw_blob, &pw_blob_avail, stream)) <= 0) return NULL;
	if (pw_blob[len-1] == '\n') pw_blob[len-1] = '\0';

	/* Tokenize */
	char *p, *tokens[8], *last;
	int i = 0;
	for ((p = strtok_r(pw_blob, ":", &last)); p;
			(p = strtok_r(NULL, ":", &last)), i++) {
		tokens[i] = p;
	}

	if (i < 8) return NULL;

	pwbuf_common(&pw_ent, tokens);

	return &pw_ent;
}

struct passwd * getpwent(void) {
	if (!pwdb) open_it();
	if (!pwdb) return NULL;
	return fgetpwent(pwdb);
}

void setpwent(void) {
	/* Reset stream to beginning */
	if (!pwdb) open_it();
	if (pwdb) rewind(pwdb);
}

void endpwent(void) {
	/* Close stream */
	if (pwdb) {
		fclose(pwdb);
		pwdb = NULL;
	}
	free(pw_blob);
	memset(&pw_ent, 0, sizeof(struct passwd));
	pw_blob = NULL;
	pw_blob_avail = 0;
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


