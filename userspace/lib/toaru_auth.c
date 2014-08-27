/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 *
 * Authentication methods
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include "lib/sha2.h"

#define MASTER_PASSWD "/etc/master.passwd"

int toaru_auth_check_pass(char * user, char * pass) {

	/* Generate SHA512 */
	char hash[SHA512_DIGEST_STRING_LENGTH];
	SHA512_Data(pass, strlen(pass), hash);

	/* Open up /etc/master.passwd */

	FILE * master = fopen(MASTER_PASSWD, "r");
	struct passwd * p;

	while ((p = fgetpwent(master))) {
		if (!strcmp(p->pw_name, user) && !strcmp(p->pw_passwd, hash)) {
			fclose(master);
			return p->pw_uid;
		}
	}

	fclose(master);
	return -1;

}

void toaru_auth_set_vars(void) {
	int uid = getuid();

	struct passwd * p = getpwuid(uid);

	if (!p) {
		char tmp[10];
		sprintf(tmp, "%d", uid);
		setenv("USER", strdup(tmp), 1);
		setenv("HOME", "/", 1);
		setenv("SHELL", "/bin/sh", 1);
	} else {
		setenv("USER", strdup(p->pw_name), 1);
		setenv("HOME", strdup(p->pw_dir), 1);
		setenv("SHELL", strdup(p->pw_shell), 1);
		setenv("WM_THEME", strdup(p->pw_comment), 1);
	}
	endpwent();

	setenv("PATH", "/usr/bin:/bin", 0);
	chdir(getenv("HOME"));
}

