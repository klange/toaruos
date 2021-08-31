/**
 * @brief List group memberships.
 * @file apps/groups.c
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>

int main(int argc, char ** argv) {
	/* First print our egid group */
	struct passwd * p = getpwuid(getegid());
	if (p) {
		fprintf(stdout, "%s ", p->pw_name);
	}
	/* Then get the group list. */
	int groupCount = getgroups(0, NULL);
	if (groupCount) {
		gid_t * myGroups = malloc(sizeof(gid_t) * groupCount);
		groupCount = getgroups(groupCount, myGroups);
		for (int i = 0; i < groupCount; ++i) {
			p = getpwuid(myGroups[i]);
			if (p) {
				fprintf(stdout, "%s ", p->pw_name);
			}
		}
	}
	fprintf(stdout,"\n");
	endpwent();
	return 0;
}

