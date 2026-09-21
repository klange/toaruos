/**
 * @brief Authentication routines.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 */
#define _TOARU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <libgen.h>
#include <errno.h>
#include <sys/stat.h>
#include <err.h>
#include "toaru/auth.h"

#ifndef fgetpwent
extern struct passwd *fgetpwent(FILE *stream);
#endif

extern int setgroups(int size, const gid_t list[]);

#define MASTER_PASSWD "/etc/master.passwd"

int toaru_auth_check_pass(char * user, char * pass) {
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

gid_t toaru_auth_get_default_group(uid_t uid) {
	FILE * master = fopen(MASTER_PASSWD, "r");
	struct passwd * p;
	while ((p = fgetpwent(master))) {
		if (p->pw_uid == uid) {
			fclose(master);
			return p->pw_gid;
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

void toaru_auth_exec_shell(int login_shell) {
	char * shell = getenv("SHELL");
	if (!shell) shell = "/bin/sh";
	char args0[1024];
	snprintf(args0, 1024,  "%s%s", login_shell ? "-" : "", basename(shell));
	char * args[] = {args0, NULL};
	execv(shell, args);
}

void toaru_auth_get_groups(uid_t uid, int *groupCount, gid_t *groups) {

	/* Get the username for this uid */
	struct passwd * pwd = getpwuid(uid);
	*groupCount = 0;
	memset(groups, 0, sizeof(gid_t) * 32);

	/* No username? No group memberships! */
	if (!pwd) return;

	/* Open the group file */
	FILE * groupList = fopen("/etc/group","r");

	/* No groups? No membership. */
	if (!groupList) return;

	/* Scan through lines of groups. */
	char * pw_blob = NULL;
	size_t avail = 0;

	while (!feof(groupList)) {
		ssize_t len;
		if ((len = getline(&pw_blob, &avail, groupList)) <= 0) break;
		if (pw_blob[len-1] == '\n') pw_blob[len-1] = '\0';

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
				if (*groupCount < 32) {
					groups[*groupCount] = groupNumber;
					(*groupCount)++;
				}
			}
		}
	}

	free(pw_blob);
	fclose(groupList);
	return;
}

void toaru_auth_set_groups(uid_t uid) {
	int groupCount = 0;
	gid_t groups[32] = {0};
	toaru_auth_get_groups(uid, &groupCount, groups);

	setgroups(groupCount, groups);
}

void toaru_set_credentials(uid_t uid, gid_t gid) {
	toaru_auth_set_groups(uid);
	setgid(gid);
	setuid(uid);
	toaru_auth_set_vars();
}

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

int toaru_auth_read_passwd(const char * which, struct PasswdEntry **out) {
	FILE * passwd = fopen(which, "r");
	if (!passwd) return -1;

	struct PasswdEntry * last = NULL;
	*out = NULL;

	while (!feof(passwd)) {
		struct PasswdEntry * ent = get_pwent(passwd);
		if (!ent) continue;
		if (!last) *out = ent;
		else last->next = ent;
		last = ent;
	}

	fclose(passwd);
	return 0;
}

struct PasswdEntry * toaru_auth_get_by_uid(struct PasswdEntry *entries, uid_t uid) {
	struct PasswdEntry * cur = entries;
	while (cur && cur->pwd.pw_uid != uid) cur = cur->next;
	return cur;
}

struct PasswdEntry * toaru_auth_get_by_name(struct PasswdEntry *entries, char * name) {
	struct PasswdEntry * cur = entries;
	while (cur && strcmp(cur->pwd.pw_name, name)) cur = cur->next;
	return cur;
}

int toaru_auth_write_passwd(const char * which, mode_t perms, struct PasswdEntry *entries) {
	/* First write to a temporary file */
	char *name = NULL;
	asprintf(&name, "%s.%d", which, getpid());

	mode_t prev = umask(S_IXUSR | S_IRWXG | S_IRWXO);
	FILE * f = fopen(name, "wx");
	umask(prev);

	if (!f) err(1, "%s", name);

	if (fchown(fileno(f), 0, 0)) err(1, "fchown");
	if (fchmod(fileno(f), perms)) err(1, "fchmod");

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

	if (rename(name, which) < 0) err(1, "rename");

	free(name);
	return 0;
}

int toaru_auth_free_passwd(struct PasswdEntry * entries) {
	while (entries) {
		struct PasswdEntry * e = entries;
		entries = e->next;
		free(e->orig_line);
		free(e);
	}

	return 0;
}

int toaru_auth_check_pass_entry(struct PasswdEntry * entry, const char * password) {
	return strcmp(entry->pwd.pw_passwd, password);
}

int toaru_auth_set_pass_entry(struct PasswdEntry * entry, char * password) {
	if (strlen(password) < 4) return ERANGE;
	if (strlen(password) > 512) return E2BIG;
	if (strchr(password, ':')) return EINVAL;
	if (strchr(password, '\n')) return EINVAL;
	entry->pwd.pw_passwd = password;
	return 0;
}
