/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2015 Kevin Lange
 *
 * Graphical login daemon.
 *
 * Launches graphical login windows and manages login sessions.
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include <sys/wait.h>

#include <toaru/auth.h>
#include <toaru/trace.h>
#define TRACE_APP_NAME "glogin"

int main (int argc, char ** argv) {
	if (getuid() != 0) {
		return 1;
	}

	/* Ensure a somewhat sane environment going in */
	TRACE("Graphical login starting.");

	setenv("USER", "root", 1);
	setenv("HOME", "/", 1);
	setenv("SHELL", "/bin/sh", 1);
	setenv("PATH", "/usr/bin:/bin", 0);
	setenv("WM_THEME", "fancy", 0);

	while (1) {

		char * username = NULL;
		char * password = NULL;
		int uid = -1;

		int com_pipe[2], rep_pipe[2];
		pipe(com_pipe);
		pipe(rep_pipe);
		TRACE("Starting login client...");

		pid_t _gui_login = fork();
		if (!_gui_login) {
			dup2(com_pipe[1], STDOUT_FILENO);
			dup2(rep_pipe[0], STDIN_FILENO);
			close(com_pipe[0]);
			close(rep_pipe[1]);
			TRACE("In client...");
			char * args[] = {"/bin/glogin-provider", NULL};
			execvp(args[0], args);
			TRACE("Exec failure?");
			exit(1);
		}

		close(com_pipe[1]);
		close(rep_pipe[0]);

		FILE * com = fdopen(com_pipe[0], "r");
		FILE * rep = fdopen(rep_pipe[1], "w");

		while (!feof(com)) {
			char buf[1024]; /* line length? */
			char * cmd = fgets(buf, sizeof(buf), com);
			size_t r = strlen(cmd);
			if (cmd && r) {
				if (cmd[r-1] == '\n') {
					cmd[r-1] = '\0';
					r--;
				}
				if (!strcmp(buf,"RESTART")) {
					TRACE("Client requested system restart, rebooting.");
					system("reboot");
				} else if (!strcmp(buf,"Hello")) {
					TRACE("Hello received from client.");
				} else if (!strncmp(buf,"USER ",5)) {
					TRACE("Username received.");
					if (username) free(username);
					username = strdup(buf + 5);
				} else if (!strncmp(buf,"PASS ",5)) {
					TRACE("Password received.");
					if (password) free(password);
					password = strdup(buf + 5);
				} else if (!strcmp(buf,"AUTH")) {
					TRACE("Perform auth request, client wants answer.");
					if (!username || !password) {
						fprintf(rep, "FAIL\n");
					} else {
						uid = toaru_auth_check_pass(username, password);
						if (uid < 0) {
							fprintf(rep, "FAIL\n");
							fflush(rep);
						} else {
							fprintf(rep, "SUCC\n");
							fflush(rep);
							break;
						}
					}
				}
			}
		}

		waitpid(_gui_login, NULL, 0);

		if (uid == -1) {
			TRACE("Not a valid session, returning login manager...");
			continue;
		}

		TRACE("Starting session...");

		pid_t _session_pid = fork();
		if (!_session_pid) {
			setuid(uid);
			toaru_auth_set_vars();
			char * args[] = {"/bin/session", NULL};
			execvp(args[0], args);
			exit(1);
		}

		waitpid(_session_pid, NULL, 0);
		TRACE("Session ended.");
	}

	return 0;
}
