/**
 * @brief Authentication Helpers
 *
 * This library allows multiple login programs (login, sudo, glogin)
 * to share authentication code by providing a single palce to check
 * passwords against /etc/master.passwd and to set typical login vars.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 */

#pragma once

#include <_cheader.h>
#include <unistd.h>
#include <pwd.h>

_Begin_C_Header

/**
 * toaru_auth_check_pass
 *
 * Returns the uid for the request user on success, -1 on failure.
 */
extern int toaru_auth_check_pass(char * user, char * pass);

/**
 * toaru_auth_set_vars
 *
 * Sets various environment variables (HOME, USER, SHELL, etc.)
 * for the current user.
 */
extern void toaru_auth_set_vars(void);

/**
 * Set supplementary groups from /etc/groups
 */
extern void toaru_auth_set_groups(uid_t uid);

/**
 * Do the above two steps, and setuid, and setgid...
 */
extern void toaru_set_credentials(uid_t uid, gid_t gid);

extern gid_t toaru_auth_get_default_group(uid_t uid);
extern void toaru_auth_get_groups(uid_t uid, int *groupCount, gid_t *groups);
extern void toaru_auth_exec_shell(int is_login);

struct PasswdEntry {
	char * orig_line;
	size_t orig_line_space;
	struct passwd pwd;
	struct PasswdEntry * next;
};

int toaru_auth_read_passwd(const char * which, struct PasswdEntry **out);
struct PasswdEntry * toaru_auth_get_by_uid(struct PasswdEntry *entries, uid_t uid);
struct PasswdEntry * toaru_auth_get_by_name(struct PasswdEntry *entries, char * name);
int toaru_auth_write_passwd(const char * which, mode_t perms, struct PasswdEntry *entries);
int toaru_auth_free_passwd(struct PasswdEntry * entries);
int toaru_auth_check_pass_entry(struct PasswdEntry * entry, const char * password);
int toaru_auth_set_pass_entry(struct PasswdEntry * entry, char * password);

_End_C_Header
