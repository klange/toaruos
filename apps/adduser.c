/**
 * @brief Add new users to password databases
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
#include <getopt.h>
#include <pwd.h>
#include <libgen.h>
#include <err.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/termios.h>
#include <toaru/auth.h>

static int usage(int argc, char * argv[]) {
	fprintf(stderr,
		"usage: %s user\n"
		"       %s --group group\n"
		"       %s user group\n",
		argv[0], argv[0], argv[0]);
	return 1;
}

static void passwd_append(struct PasswdEntry * entries, struct PasswdEntry * newentry) {
	if (!entries) errx(1, "No entries?");
	while (entries->next) entries = entries->next;
	entries->next = newentry;
}

static void group_append(struct GroupEntry * groups, struct GroupEntry * newgroup) {
	if (!groups) errx(1, "No groups?");
	while (groups->next) groups = groups->next;
	groups->next = newgroup;
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

enum option_vals {
	OPT_UID = 1000,
	OPT_GID,
	OPT_SHELL,
	OPT_HOME,
	OPT_GECOS,
	OPT_THEME,
	OPT_GROUP,
	OPT_HELP,
};

int main(int argc, char * argv[]) {
	int opt;

	pid_t opt_uid = -1;
	gid_t opt_gid = -1;
	char * opt_shell = NULL;
	char * opt_home  = NULL;
	char * opt_gecos = NULL;
	char * opt_theme = NULL;
	int is_addgroup = !strcmp(basename(argv[0]),"addgroup");

	struct option long_opts[] = {
		{"uid",    required_argument, 0, OPT_UID},
		{"gid",    required_argument, 0, OPT_GID},
		{"shell",  required_argument, 0, OPT_SHELL},
		{"home",   required_argument, 0, OPT_HOME},
		{"gecos",  required_argument, 0, OPT_GECOS},
		{"theme",  required_argument, 0, OPT_THEME},
		{"group",  no_argument,       0, OPT_GROUP},
		{"help",   no_argument,       0, OPT_HELP},
		{0,0,0,0},
	};

	while ((opt = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
		switch (opt) {
			case OPT_UID:
				opt_uid = atoi(optarg);
				break;

			case OPT_GID:
				opt_gid = atoi(optarg);
				break;

			case OPT_SHELL:
				opt_shell = optarg;
				break;

			case OPT_HOME:
				opt_home = optarg;
				break;

			case OPT_GECOS:
				opt_gecos = optarg;
				break;

			case OPT_THEME:
				opt_theme = optarg;
				break;

			case OPT_GROUP:
				is_addgroup = 1;
				break;

			default:
				return usage(argc, argv);
		}
	}

	if (geteuid() != 0) errx(54, "must be root");

	if (optind + 2 < argc) return usage(argc, argv); /* excess arguments */

	char * name = argv[optind];

	struct PasswdEntry * shadow_entries = NULL;
	struct PasswdEntry * master_entries = NULL;
	struct GroupEntry * groups;

	if (toaru_auth_read_passwd("/etc/passwd", &shadow_entries) < 0) err(1, "/etc/passwd");
	if (toaru_auth_read_passwd("/etc/master.passwd", &master_entries) < 0) err(1, "/etc/master.passwd");
	if (toaru_auth_read_group("/etc/group", &groups) < 0) err(1, "/etc/group");

	if (optind + 2 == argc) {
		if (is_addgroup) return usage(argc, argv);
		char * group_name = argv[optind+1];

		/* Add user to group */
		struct PasswdEntry * user = toaru_auth_get_by_name(master_entries, name);
		if (!user) errx(12, "user does not exist");

		struct GroupEntry * group = toaru_auth_get_group_by_name(groups, group_name);
		if (!group) errx(12, "group does not exist");

		size_t grp_count = 0;
		for (char ** mem = group->grp.gr_mem; *mem; mem++) {
			if (!strcmp(*mem, name)) errx(1, "'%s' is already in '%s'", name, group_name);
			grp_count++;
		}

		char ** nmem = calloc(grp_count + 2, sizeof(char*));
		char ** p = nmem;
		for (char ** mem = group->grp.gr_mem; *mem; mem++, nmem++) {
			*nmem = *mem;
		}
		*nmem = name;
		group->grp.gr_mem = p;

		if (toaru_auth_write_group("/etc/group", 644, groups)) err(1, "failed to write group database");

		return 0;
	}

	if (!toaru_auth_validate_name(name)) errx(31, "name contains invalid characters");

	/* Reject name that already exists in shadow or master */
	if (toaru_auth_get_by_name(shadow_entries, name)) errx(11, "'%s' already exists", name);
	if (toaru_auth_get_by_name(master_entries, name)) errx(11, "'%s' already exists", name);

	struct PasswdEntry * shadow_entry = calloc(1, sizeof(struct PasswdEntry));
	passwd_append(shadow_entries, shadow_entry);

	if (is_addgroup && opt_gid != -1) {
		opt_uid = opt_gid;
	}

	if (opt_uid == -1) {
		uid_t highest = 0;
		for (struct PasswdEntry * e = shadow_entries; e; e = e->next) {
			if (is_addgroup && e->pwd.pw_uid >= 1000) continue;
			if (e->pwd.pw_uid > highest) highest = e->pwd.pw_uid;
		}

		opt_uid = highest + 1;
	}

	if (opt_gid == -1) {
		opt_gid = opt_uid;
	}

	if (opt_home == NULL) {
		if (is_addgroup) {
			opt_home = "/tmp";
		} else {
			asprintf(&opt_home, "/home/%s", name);
		}
	}

	if (opt_gecos == NULL) {
		opt_gecos = name;
	}

	if (opt_shell == NULL) {
		if (is_addgroup) {
			opt_shell = "/bin/false";
		} else {
			opt_shell = "/bin/sh";
		}
	}

	if (opt_theme == NULL) {
		if (is_addgroup) {
			opt_theme = "none";
		} else {
			opt_theme = "fancy";
		}
	}

	/* Validate things aren't empty */
	if (!*opt_gecos) errx(1, "gecos unset");
	if (!*opt_home)  errx(1, "home unset");
	if (!*opt_shell) errx(1, "shell unset");
	if (!*opt_theme) errx(1, "theme unset");

	shadow_entry->pwd.pw_name    = name;
	shadow_entry->pwd.pw_passwd  = "x";
	shadow_entry->pwd.pw_uid     = opt_uid;
	shadow_entry->pwd.pw_gid     = opt_gid;
	shadow_entry->pwd.pw_gecos   = opt_gecos;
	shadow_entry->pwd.pw_shell   = opt_shell;
	shadow_entry->pwd.pw_dir     = opt_home;
	shadow_entry->pwd.pw_comment = opt_theme;

	if (!is_addgroup) {
		struct PasswdEntry * master_entry = calloc(1, sizeof(struct PasswdEntry));
		passwd_append(master_entries, master_entry);

		master_entry->pwd.pw_name    = name;
		master_entry->pwd.pw_uid     = opt_uid;
		master_entry->pwd.pw_gid     = opt_gid;
		master_entry->pwd.pw_gecos   = opt_gecos;
		master_entry->pwd.pw_shell   = opt_shell;
		master_entry->pwd.pw_dir     = opt_home;
		master_entry->pwd.pw_comment = opt_theme;

		mkdir(opt_home, 0750);
		chown(opt_home, opt_uid, opt_gid);
		chmod(opt_home, 0750);

		char new_password[1024] = {0};
		char confirm_password[1024] = {0};
		if (prompt_for_password("Enter password: ", new_password)) errx(1, "cancelled");
		if (prompt_for_password("Confirm password: ", confirm_password)) errx(1, "cancelled");
		if (strcmp(new_password, confirm_password)) errx(1, "passwords do not match");

		int result = toaru_auth_set_pass_entry(master_entry, new_password);

		if (result) {
			switch (result) {
				case ERANGE: errx(1, "Password is too short.");
				case E2BIG:  errx(1, "Password is too long.");
				case EINVAL: errx(1, "Password contains invalid characters.");
				default: errx(1, "Unknown error.");
			}
		}

		if (toaru_auth_write_passwd("/etc/master.passwd", 0600, master_entries)) err(1, "failed to write password database");
		if (toaru_auth_write_passwd("/etc/passwd", 0644, shadow_entries)) err(1, "failed to write user database");
	} else {

		/* Groups do not get home directories and we don't put passwords on groups (they can't log in) */
		struct GroupEntry * new_group = calloc(1, sizeof(struct GroupEntry));

		new_group->grp.gr_name    = name;
		new_group->grp.gr_passwd  = "x";
		new_group->grp.gr_gid     = opt_gid;
		new_group->grp.gr_mem     = calloc(1, sizeof(char*));

		group_append(groups, new_group);

		if (toaru_auth_write_group("/etc/group", 644, groups)) err(1, "failed to write group database");
		if (toaru_auth_write_passwd("/etc/passwd", 0644, shadow_entries)) err(1, "failed to write user database");
	}

	return 0;
}
