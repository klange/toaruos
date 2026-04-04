/**
 * @brief Show user group identifiers
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
#include <toaru/auth.h>

static int usage(char ** argv) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - show group identifiers\n"
			"\n"
			"usage: %s [" X_S "user" X_E "]...\n"
			"       %s -G [-n] [" X_S "user" X_E "]\n"
			"       %s -g [-nr] [" X_S "user" X_E "]\n"
			"       %s -u [-nr] [" X_S "user" X_E "]\n"
			"\n"
			" -G     " X_S "print all the different groups" X_E "\n"
			" -g     " X_S "print the effective group" X_E "\n"
			" -u     " X_S "print the effective user" X_E "\n"
			" -n     " X_S "print names instead of IDs" X_E "\n"
			" -n     " X_S "print real rather than effective IDs" X_E "\n"
			" -?     " X_S "show this help text" X_E "\n"
			"\n", argv[0], argv[0], argv[0], argv[0], argv[0]);
	return 1;
}

static void print_group(gid_t gid, int use_names) {
	if (use_names) {
		struct passwd * pwent = getpwuid(gid);
		printf("%s", pwent ? pwent->pw_name : "");
	} else {
		printf("%u", gid);
	}
}

static void print_group_ext(gid_t gid, char *name) {
	if (name) {
		printf("%s=", name);
	}
	print_group(gid, 0);
	struct passwd * pwent = getpwuid(gid);
	if (pwent) {
		printf("(%s)", pwent->pw_name);
	}
}

int main(int argc, char * argv[]) {
	int opt;
	int all_groups = 0;
	int only_effective = 0;
	int only_effective_user = 0;
	int use_names = 0;
	int use_real = 0;

	while ((opt = getopt(argc, argv, "Ggunr?")) != -1) {
		switch (opt) {
			case 'G':
				if (only_effective || only_effective_user) return usage(argv);
				all_groups = 1;
				break;
			case 'g':
				if (all_groups || only_effective_user) return usage(argv);
				only_effective = 1;
				break;
			case 'u':
				if (all_groups || only_effective) return usage(argv);
				only_effective_user = 1;
				break;
			case 'n':
				use_names = 1;
				break;
			case 'r':
				use_real = 1;
				break;
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) {
		if (only_effective_user) {
			print_group(use_real ? getuid() : geteuid(), use_names);
		} else if (only_effective) {
			print_group(use_real ? getgid() : getegid(), use_names);
		} else if (all_groups) {
			if (use_real) return usage(argv);
			gid_t gid = getgid();
			gid_t egid = getegid();
			print_group(gid, use_names);
			if (egid != gid) {
				printf(" ");
				print_group(egid, use_names);
			}
			int groupCount = getgroups(0, NULL);
			if (groupCount) {
				gid_t * myGroups = malloc(sizeof(gid_t) * groupCount);
				groupCount = getgroups(groupCount, myGroups);
				for (int i = 0; i < groupCount; ++i) {
					if (myGroups[i] != gid && myGroups[i] != egid) {
						printf(" ");
						print_group(myGroups[i], use_names);
					}
				}
				free(myGroups);
			}
		} else {
			if (use_real || use_names) return usage(argv);
			print_group_ext(getuid(),"uid");
			printf(" ");
			print_group_ext(getgid(),"gid");

			if (geteuid() != getuid()) {
				printf(" ");
				print_group_ext(geteuid(),"euid");
			}

			if (getegid() != getgid()) {
				printf(" ");
				print_group_ext(getegid(),"egid");
			}

			int groupCount = getgroups(0, NULL);
			if (groupCount) {
				printf(" groups=");
				gid_t * myGroups = malloc(sizeof(gid_t) * groupCount);
				groupCount = getgroups(groupCount, myGroups);
				for (int i = 0; i < groupCount; ++i) {
					if (i > 0) printf(",");
					print_group_ext(myGroups[i],NULL);
				}
				free(myGroups);
			}
		}
	} else if (optind + 1 == argc) {
		struct passwd * pwd = getpwnam(argv[optind]);
		if (!pwd) {
			fprintf(stderr, "%s: %s: no such user\n", argv[0], argv[optind]);
			return 1;
		}

		gid_t uid = pwd->pw_uid;
		gid_t gid = pwd->pw_gid;

		if (only_effective_user) {
			print_group(uid, use_names);
		} else if (only_effective) {
			print_group(gid, use_names);
		} else if (all_groups) {
			if (use_real) return usage(argv);
			print_group(gid, use_names);

			int groupCount = 0;
			gid_t groups[32];
			toaru_auth_get_groups(uid, &groupCount, groups);
			for (int i = 0; i < groupCount; ++i) {
				printf(" ");
				print_group(groups[i], use_names);
			}
		} else {
			if (use_real || use_names) return usage(argv);
			print_group_ext(uid,"uid");
			printf(" ");
			print_group_ext(gid,"gid");
			int groupCount = 0;
			gid_t groups[32];
			toaru_auth_get_groups(uid, &groupCount, groups);
			if (groupCount) {
				printf(" groups=");
				for (int i = 0; i < groupCount; ++i) {
					if (i > 0) printf(",");
					print_group_ext(groups[i],NULL);
				}
			}
		}
	} else {
		return usage(argv);
	}

	printf("\n");
	return 0;
}
