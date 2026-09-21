/**
 * @brief Minimal shell change utility
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#define _TOARU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <err.h>
#include <sys/stat.h>
#include <toaru/auth.h>

static int usage(int argc, char * argv[]) {
	fprintf(stderr,
		"usage: %s [-s shell] [-t theme] [user]\n",
		argv[0]);
	return 1;
}

struct ValidShell {
	char * shell;
	struct ValidShell * next;
};

static struct ValidShell * shells = NULL;

static int read_shells(void) {
	FILE * etc_shells = fopen("/etc/shells", "r");
	if (!etc_shells) return -1;

	struct ValidShell * last = NULL;

	char * blob = NULL;
	size_t avail = 0;
	ssize_t len;

	while (!feof(etc_shells)) {
		if ((len = getline(&blob, &avail, etc_shells)) <= 0) break;
		if (blob[len-1] == '\n') blob[len-1] = '\0';
		if (blob[0] == '#') continue;

		struct ValidShell * shell = calloc(1, sizeof(struct ValidShell));
		shell->shell = strdup(blob);

		if (!last) shells = shell;
		else last->next = shell;
		last = shell;
	}

	fclose(etc_shells);
	return 0;
}

static int validate_shell(const char * desired_shell) {
	struct ValidShell * valid = shells;

	while (valid) {
		if (!strcmp(valid->shell, desired_shell)) return 1;
		valid = valid->next;
	}

	return 0;
}

int main(int argc, char * argv[]) {
	if (geteuid() != 0) errx(1, "euid is not root");

	char * desired_shell = NULL;
	char * desired_theme = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "s:t:")) != -1) {
		switch (opt) {
			case 's':
				desired_shell = optarg;
				break;
			case 't':
				desired_theme = optarg;
				break;
			default:
				return usage(argc, argv);
		}
	}

	if (optind != argc && optind + 1 != argc) return usage(argc, argv); /* excess args */

	struct PasswdEntry * entries = NULL;

	if (toaru_auth_read_passwd("/etc/passwd", &entries) < 0) err(1, "/etc/passwd");
	if (read_shells() < 0) err(1, "/etc/shells");

	uid_t me = getuid();

	struct PasswdEntry * entry = NULL;
	if (optind != argc) {
		entry = toaru_auth_get_by_name(entries, argv[optind]);
	} else {
		entry = toaru_auth_get_by_uid(entries, me);
	}

	if (!entry) errx(1, "user not found");
	if (me != 0 && me != entry->pwd.pw_uid) errx(2, "can not modify other user's shell");

	if (!desired_shell && !desired_theme)  {
		/* print them and ask */
		fprintf(stderr, "Login Shell [%s]: ", entry->pwd.pw_shell);
		fflush(stderr);

		size_t avail = 0;
		ssize_t len;
		if ((len = getline(&desired_shell, &avail, stdin)) < 0) err(1, "unexpected eof");
		if (len > 0 && desired_shell[len-1] == '\n') {
			desired_shell[len-1] = '\0';
			len--;
		}

		if (len == 0) return 0; /* Blank entry, do nothing. */
	}

	if (desired_shell) {
		/* Check if the desired shell is acceptable. */
		if (me != 0 && !validate_shell(desired_shell)) errx(1, "%s is an invalid shell", desired_shell);
		entry->pwd.pw_shell = desired_shell;
	}

	if (desired_theme) {
		for (char *t = desired_theme; *t; t++) if (*t < 'a' || *t > 'z') errx(2, "%s is an invalid theme", desired_theme);
		entry->pwd.pw_comment = desired_theme;
	}

	return toaru_auth_write_passwd("/etc/passwd", 0644, entries);
}
