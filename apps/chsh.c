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

static int usage(int argc, char * argv[]) {
	fprintf(stderr,
		"usage: %s [-s shell] [-t theme] [user]\n",
		argv[0]);
	return 1;
}

struct PasswdEntry {
	char * orig_line;
	size_t orig_line_space;
	struct passwd pwd;
	struct PasswdEntry * next;
};

static struct PasswdEntry * entries = NULL;

static struct PasswdEntry * get_pwent(FILE * stream) {
	struct PasswdEntry * result = calloc(1, sizeof(struct PasswdEntry));
	struct passwd * _result = NULL;

	if (fgetpwent_t(stream, &result->pwd, &result->orig_line, &result->orig_line_space, &_result)) {
		free(result->orig_line);
		free(result);
		return NULL;
	}

	return result;
}

static int read_passwd(void) {
	FILE * passwd = fopen("/etc/passwd", "r");
	if (!passwd) return -1;

	struct PasswdEntry * last = NULL;

	while (!feof(passwd)) {
		struct PasswdEntry * ent = get_pwent(passwd);
		if (!ent) continue;
		if (!last) entries = ent;
		else last->next = ent;
		last = ent;
	}

	fclose(passwd);

	return 0;
}

static struct PasswdEntry * get_by_uid(uid_t uid) {
	struct PasswdEntry * cur = entries;
	while (cur && cur->pwd.pw_uid != uid) cur = cur->next;
	return cur;
}

static struct PasswdEntry * get_by_name(char * name) {
	struct PasswdEntry * cur = entries;
	while (cur && strcmp(cur->pwd.pw_name, name)) cur = cur->next;
	return cur;
}

static int write_passwd(void) {
	/* First write to a temporary file */
	char *name = NULL;
	asprintf(&name, "/etc/passwd.%d", getpid());

	FILE * f = fopen(name, "wx");
	if (!f) err(1, "%s", name);

	if (fchown(fileno(f), 0, 0)) err(1, "fchown");
	if (fchmod(fileno(f), 0644)) err(1, "fchmod");

	struct PasswdEntry * ent = entries;

	while (ent) {
		fprintf(f, "%s:%s:%d:%d:%s:%s:%s:%s\n",
			ent->pwd.pw_name, ent->pwd.pw_passwd,
			ent->pwd.pw_uid, ent->pwd.pw_gid,
			ent->pwd.pw_gecos, ent->pwd.pw_dir,
			ent->pwd.pw_shell, ent->pwd.pw_comment);

		ent = ent->next;
	}

	fflush(f);
	fclose(f);

	if (rename(name, "/etc/passwd") < 0) err(1, "rename");

	free(name);
	return 0;
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

	if (read_passwd() < 0) err(1, "/etc/passwd");
	if (read_shells() < 0) err(1, "/etc/shells");

	uid_t me = getuid();

	struct PasswdEntry * entry = NULL;
	if (optind != argc) {
		entry = get_by_name(argv[optind]);
	} else {
		entry = get_by_uid(me);
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

	return write_passwd();
}
