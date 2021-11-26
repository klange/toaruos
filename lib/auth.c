/**
 * @brief Authentication routines.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#ifndef fgetpwent
extern struct passwd *fgetpwent(FILE *stream);
#endif

extern int setgroups(int size, const gid_t list[]);

#define MASTER_PASSWD "/etc/master.passwd"

int toaru_auth_check_pass(char * user, char * pass) {

	/* XXX DO something useful */

	/* Open up /etc/master.passwd */

	FILE * master = fopen(MASTER_PASSWD, "r");
	struct passwd * p;

	while ((p = fgetpwent(master))) {
		if (!strcmp(p->pw_name, user) && !strcmp(p->pw_passwd, pass)) {
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

void toaru_auth_set_groups(uid_t uid) {
	/* Get the username for this uid */
	struct passwd * pwd = getpwuid(uid);

	/* No username? No group memberships! */
	if (!pwd) goto no_groups;

	/* Open the group file */
	FILE * groupList = fopen("/etc/group","r");

	/* No groups? No membership. */
	if (!groupList) goto no_groups;

	/* Scan through lines of groups. */
#define LINE_LEN 2048
	char * pw_blob = malloc(LINE_LEN);

	int groupCount = 0;
	gid_t myGroups[32] = {0};

	while (!feof(groupList)) {
		memset(pw_blob, 0x00, LINE_LEN);
		fgets(pw_blob, LINE_LEN, groupList);
		if (pw_blob[strlen(pw_blob)-1] == '\n') {
			pw_blob[strlen(pw_blob)-1] = '\0'; /* erase newline */
		}

		/* Tokenize */
		char * memberlist = NULL;
		char *p, *last;
		gid_t groupNumber = -1;
		int i = 0;
		for ((p = strtok_r(pw_blob, ":", &last)); p;
				(p = strtok_r(NULL, ":", &last)), i++) {
			if (i == 2) {
				groupNumber = atoi(p);
			} else if (i == 3) {
				memberlist = p;
				break;
			}
		}

		if (groupNumber == -1) continue;
		if (!memberlist) continue;

		for ((p = strtok_r(memberlist, ",", &last)); p;
				(p = strtok_r(NULL, ",", &last))) {
			if (!strcmp(p, pwd->pw_name)) {
				if (groupCount < 32) {
					myGroups[groupCount] = groupNumber;
					groupCount++;
				}
			}
		}
	}

	setgroups(groupCount, myGroups);
	free(pw_blob);
	fclose(groupList);
	return;

no_groups:
	setgroups(0, NULL);
}

void toaru_set_credentials(uid_t uid) {
	toaru_auth_set_groups(uid);
	setgid(uid);
	setuid(uid);
	toaru_auth_set_vars();
}
