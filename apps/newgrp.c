/**
 * @brief Change real and effective group.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#define _TOARU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <toaru/auth.h>

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-l] [group]\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	for (int i = 0; i < 3; ++i) {
		if (fcntl(i, F_GETFL, 0) == -1) {
			if (open("/dev/null", O_RDWR) != i) abort();
		}
	}

	int as_login = 0;

	int opt;
	while ((opt = getopt(argc, argv, "l")) != -1) {
		switch (opt) {
			case 'l':
				as_login = 1;
				break;
			default:
				return usage(argv);
		}
	}

	uid_t me = getuid();

	/* What group are we in by default? */
	gid_t default_group = toaru_auth_get_default_group(me);
	gid_t desired_group = default_group;

	/* What groups can we otherwise be in? */
	int groupCount = 0;
	gid_t groups[32] = {0};
	toaru_auth_get_groups(me, &groupCount, groups);

	/* What group does the user want to set? */
	if (optind < argc) {
		struct passwd * pw = getpwnam(argv[optind]);
		if (!pw) {
			fprintf(stderr, "%s: group '%s' does not exist\n", argv[0], argv[optind]);
			return 1;
		}
		desired_group = pw->pw_uid;
		endpwent();
	}

	/* Can the user be in that group? */
	if (desired_group != default_group) {
		int found = 0;
		for (int i = 0; i < groupCount; ++i) {
			if (groups[i] == desired_group) {
				groups[i] = default_group;
				found = 1;
				break;
			}
		}

		if (!found) {
			fprintf(stderr, "%s: access denied\n", argv[0]);
			return 1;
		}
	}

	setgroups(groupCount, groups);
	setgid(desired_group);
	setuid(me);

	if (as_login) toaru_auth_set_vars();

	toaru_auth_exec_shell(as_login);

	return 127;
}
