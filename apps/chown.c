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
#include <fcntl.h>
#include <sys/stat.h>

static int verbosity = 0;
static int change_links = 0;   /* -h */
static int recurse = 0;        /* -R  */
static int recurse_mode = 0; /* -H -L -P */

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [OWNER][:[GROUP]] FILE...\n", argv[0]);
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

#define do_error() return fprintf(stderr, "%s: %s: %s\n", argv[0], thing, strerror(errno)), 1

static int change_thing(char * argv[], const char * thing, uid_t user, gid_t group, int dereference) {
	struct stat st;

	if (dereference) {
		if (stat(thing, &st)) do_error();
	} else {
		if (lstat(thing, &st)) do_error();
	}

	if (dereference && S_ISLNK(st.st_mode)) {
		if (lchown(thing, user, group)) do_error();
		goto report_results;
	}

	if (chown(thing, user, group)) do_error();

	if (recurse && S_ISDIR(st.st_mode)) {
		fprintf(stderr, "%s: warning: recursion into '%s' ignored\n", argv[0], thing);
	}

report_results:
	if (verbosity) fprintf(stdout, "%s\n", thing);
	return 0;
}

int main(int argc, char * argv[]) {
	int opt;
	while ((opt = getopt(argc,argv,"hHLPRv")) != -1) {
		switch (opt) {
			case 'h':
				change_links = 1;
				break;
			case 'H': /* Follow links on the command line. */
			case 'L': /* Follow all links. */
			case 'P': /* Follow no links. */
				recurse_mode = opt;
				break;
			case 'R':
				recurse = 1;
				break;
			case 'v':
				if (verbosity) fprintf(stderr, "%s: warning: extra verbosity not yet supported\n", argv[0]);
				verbosity++;
				break;
			case '?':
				return usage(argv);
		}
	}

	/* Need at least the user/group spec and one file */
	if (optind + 1 >= argc) return usage(argv);
	if (recurse_mode && !recurse) fprintf(stderr, "%s: warning: -H/-L/-P are only meaningful with -R\n", argv[0]); /* warning, not error */
	if (recurse && !recurse_mode) recurse_mode = 'P'; /* default */

	/* XXX todo */
	if (recurse) fprintf(stderr, "%s: warning: recursion unsupported; each directory will yield an additional diagnostic\n", argv[0]);

	uid_t user = -1;
	uid_t group = -1;

	if (parse_user_group(argv, argv[optind++], &user, &group)) return 1;
	if (user == -1 && group == -1) return 0;

	int retval = 0;

	for (; optind < argc; optind++) {
		retval |= change_thing(argv, argv[optind], user, group, (!recurse && !change_links) || (recurse_mode != 'P'));
	}

	return retval;
}
