/**
 * @brief chown - bad implementation thereof
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [OWNER][:[GROUP]] FILE...\n", argv[0]);
	return 1;
}

static int invalid(char * argv[], char c) {
	fprintf(stderr, "%s: %c: unrecognized option\n", argv[0], c);
	return 1;
}

static int parse_user_group(char * argv[], char * arg, uid_t * user, gid_t * group) {

	/* Does this look like a number? */
	if (*arg >= '0' && *arg <= '9') {
		/* Try to extract */
		char * endptr;
		unsigned long int number = strtoul(arg, &endptr, 10);
		if (*endptr != ':' && *endptr != '\0') {
			fprintf(stderr, "%s: %s: Invalid user/group specification\n", argv[0], arg);
		}
		*user = number;
		arg = endptr;
	} else if (*arg == ':') {
		*user = -1;
		arg++;
	} else {
		char * colon = strstr(arg, ":");
		if (colon) {
			*colon = '\0';
		}
		/* Check name */
		struct passwd * userEnt = getpwnam(arg);
		if (!userEnt) {
			fprintf(stderr, "%s: %s: Invalid user\n", argv[0], arg);
			return 1;
		}
		*user = userEnt->pw_uid;
		if (colon) {
			arg = colon + 1;
		} else {
			arg = NULL;
		}
	}

	if (arg && *arg) {
		if (*arg >= '0' && *arg <= '9') {
			char * endptr;
			unsigned long int number = strtoul(arg, &endptr, 10);
			if (*endptr != '\0') {
				fprintf(stderr, "%s: %s: Invalid group specification\n", argv[0], arg);
			}
			*group = number;
			arg = endptr;
		} else {
			struct passwd * userEnt = getpwnam(arg);
			if (!userEnt) {
				fprintf(stderr, "%s: %s: Invalid group\n", argv[0], arg);
				return 1;
			}
			*group = userEnt->pw_uid;
		}
	}

	return 0;
}

int main(int argc, char * argv[]) {

	int i = 1;
	for (; i < argc; i++) {
		if (argv[i][0] != '-') break;

		switch (argv[i][0]) {
			case 'h':
				return usage(argv);
			default:
				return invalid(argv,argv[i][0]);
		}
	}

	if (i + 1 >= argc) return usage(argv);

	uid_t user = -1;
	uid_t group = -1;

	if (parse_user_group(argv, argv[i++], &user, &group)) return 1;
	if (user == -1 && group == -1) return 0;

	int retval = 0;

	for (; i < argc; i++) {
		if (chown(argv[i], user, group)) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			retval = 1;
		}
	}

	return retval;
}
