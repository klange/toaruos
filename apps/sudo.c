/**
 * @brief sudo - Run processes as the root user, after authenticating.
 *
 * Our sudo supports cached authentication, so you don't need to keep
 * entering your password.
 *
 * Probably terribly insecure, but our main password auth function is
 * a plain text comparison, so *shrug*.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 K. Lange
 */
#define _TOARU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <errno.h>
#include <pwd.h>
#include <dirent.h>
#include <getopt.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <toaru/auth.h>

struct SudoContext {
	const char * program_name;
	int sudo_as_shell;
	int (*prompt_callback)(struct SudoContext * ctx, char * username, char * password, int failures);
	char **argv;
	int argc;
};

#define MINUTES * 60

#define SUDO_TIME 5 MINUTES

static int sudo_loop(struct SudoContext * ctx) {

	int fails = 0;

	if (geteuid() != 0) {
		fprintf(stderr, "%s: effective uid is not 0\n", ctx->program_name);
		return 1;
	}

	struct stat buf;
	if (stat("/var/sudoers", &buf)) {
		mkdir("/var/sudoers", 0700);
	}

	if (ctx->sudo_as_shell) {
		char * shell = getenv("SHELL");
		if (!shell) shell = "/bin/sh";
		if (ctx->argc != 0) {
			char ** argv = ctx->argv;
			ctx->argv = calloc(4,sizeof(char**));
			ctx->argv[0] = shell;
			ctx->argv[1] = "-c";

			for (int pass = 0; pass < 2; ++pass) {
				size_t count = 0;
				for (int i = 0; i < ctx->argc; ++i) {
					for (char * c = argv[i]; *c; ++c) {
						if (*c < 'a' && *c > 'z' && *c < 'A' && *c > 'Z' && *c < '0' && *c > '9' && *c != '_' && *c != '-' && *c != '$') {
							if (ctx->argv[2]) ctx->argv[2][count] = '\\';
							count++;
						}
						if (ctx->argv[2]) ctx->argv[2][count] = *c;
						count++;
					}
					if (i + 1 != ctx->argc) {
						if (ctx->argv[2]) ctx->argv[2][count] = ' ';
						count++;
					}
				}

				if (pass == 0) ctx->argv[2] = calloc(1,count + 1);
			}
		} else {
			ctx->argv = calloc(2,sizeof(char*));
			ctx->argv[0] = shell;
		}
	}

	while (1) {
		int need_password = 1;
		int need_sudoers  = 1;

		uid_t me = getuid();
		if (me == 0) {
			need_password = 0;
			need_sudoers  = 0;
		}

		struct passwd * p = getpwuid(me);
		if (!p) {
			fprintf(stderr, "%s: unable to obtain username for real uid=%d\n", ctx->program_name, getuid());
			return 1;
		}
		char * username = strdup(p->pw_name);

		char token_file[64];
		sprintf(token_file, "/var/sudoers/%d-%d", me, getsid(0));

		if (need_password) {
			struct stat buf;
			if (!stat(token_file, &buf)) {
				/* check the time */
				if (buf.st_mtime > (SUDO_TIME) && time(NULL) - buf.st_mtime < (SUDO_TIME)) {
					need_password = 0;
				}
			}
		}

		if (need_password) {
			char * password = calloc(1024, sizeof(char));

			if (ctx->prompt_callback(ctx, username, password, fails)) {
				free(username);
				free(password);
				return 1;
			}

			int uid = toaru_auth_check_pass(username, password);

			free(password);

			if (uid < 0) {
				free(username);
				fails++;
				if (fails == 3) {
					fprintf(stderr, "%s: %d incorrect password attempts\n", ctx->program_name, fails);
					return 1;
				}
				fprintf(stderr, "Sorry, try again.\n");
				continue;
			}
		}

		/* Determine if this user is in the sudoers file */
		if (need_sudoers) {
			FILE * sudoers = fopen("/etc/sudoers","r");
			if (!sudoers) {
				free(username);
				fprintf(stderr, "%s: /etc/sudoers is not available\n", ctx->program_name);
				return 1;
			}

			/* Read each line */
			int in_sudoers = 0;
			while (!feof(sudoers)) {
				char line[1024];
				fgets(line, 1024, sudoers);
				char * nl = strchr(line, '\n');
				if (nl) {
					*nl = '\0';
				}
				if (!strncmp(line,username,1024)) {
					in_sudoers = 1;
					break;
				}
			}
			fclose(sudoers);

			if (!in_sudoers) {
				fprintf(stderr, "%s is not in sudoers file.\n", username);
				free(username);
				return 1;
			}
		}

		free(username);

		/* Write a timestamp file */
		FILE * f = fopen(token_file, "w");
		if (!f) {
			fprintf(stderr, "%s: (warning) failed to create token file\n", ctx->program_name);
		}
		fclose(f);

		/* Set username to root */
		putenv("USER=root");

		/* Actually become root, so real user id = 0 */
		setgid(0);
		setuid(0);
		setgroups(0,NULL);

		execvp(ctx->argv[0], ctx->argv);

		/* XXX: There are other things that can cause an exec to fail. */
		fprintf(stderr, "%s: %s: command not found\n", ctx->program_name, ctx->argv[0]);
		return 1;
	}

	return 0;
}

static int basic_callback(struct SudoContext * ctx, char * username, char * password, int fails) {
	fprintf(stderr, "[%s] password for %s: ", ctx->program_name, username);
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

int usage(int argc, char * argv[]) {
	fprintf(stderr,
		"usage: %s command...\n"
		"       %s -s [command...]\n",
		argv[0], argv[0]);
	return 1;
}

int main(int argc, char ** argv) {
	struct SudoContext ctx = {0};
	ctx.program_name = argv[0];
	ctx.prompt_callback = basic_callback;

	static struct option long_opts[] = {
		{"help",  no_argument, 0, 'h'},
		{"shell", no_argument, 0, 's'},
		{0,0,0,0},
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "+hs", long_opts, NULL)) != -1) {
		switch (opt) {
			case 'h':
				usage(argc, argv);
				return 0;
			case 's':
				ctx.sudo_as_shell = 1;
				break;
			default:
				return usage(argc, argv);
		}
	}

	if (!ctx.sudo_as_shell && optind == argc) return usage(argc, argv);
	ctx.argv = &argv[optind];
	ctx.argc = argc - optind;

	return sudo_loop(&ctx);
}

