/**
 * @brief Change user password
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
#include <sys/termios.h>
#include <toaru/auth.h>

static int usage(int argc, char * argv[]) {
	fprintf(stderr,
		"usage: %s [user]\n",
		argv[0]);
	return 1;
}

static int prompt_for_password(const char * prompt, char * password) {
	fprintf(stderr, "%s", prompt);
	fflush(stderr);

	/* Disable echo */
	struct termios old, new;
	tcgetattr(fileno(stdin), &old);
	new = old;
	new.c_lflag &= (~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);

	fgets(password, 1024, stdin);
	if (feof(stdin)) return 1;

	password[strlen(password)-1] = '\0';
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
	fprintf(stderr, "\n");

	return 0;
}

int main(int argc, char * argv[]) {
	if (geteuid() != 0) errx(1, "euid is not root");

	int opt;
	while ((opt = getopt(argc, argv, "")) != -1) {
		/* no options */
		return usage(argc, argv);
	}

	if (optind != argc && optind + 1 != argc) return usage(argc, argv); /* excess args */

	struct PasswdEntry * entries = NULL;

	if (toaru_auth_read_passwd("/etc/master.passwd", &entries) < 0) err(1, "/etc/master.passwd");

	uid_t me = getuid();

	struct PasswdEntry * entry = NULL;
	if (optind != argc) {
		entry = toaru_auth_get_by_name(entries, argv[optind]);
	} else {
		entry = toaru_auth_get_by_uid(entries, me);
	}

	if (!entry) errx(1, "user not found");
	if (me != 0 && me != entry->pwd.pw_uid) errx(2, "can not set other user's password");

	if (me == entry->pwd.pw_uid) {
		char current_password[1024] = {0};
		if (prompt_for_password("Enter current password: ", current_password)) errx(1, "cancelled");
		if (toaru_auth_check_pass_entry(entry, current_password)) errx(1, "authentication failed");
	}

	char new_password[1024] = {0};
	char confirm_password[1024] = {0};

	if (prompt_for_password("Enter new password: ", new_password)) errx(1, "cancelled");
	if (prompt_for_password("Confirm password: ", confirm_password)) errx(1, "cancelled");
	if (strcmp(new_password, confirm_password)) errx(1, "passwords do not match");
	if (toaru_auth_set_pass_entry(entry, new_password)) errx(1, "failed to set password");

	return toaru_auth_write_passwd("/etc/master.passwd", 0600, entries);
}

