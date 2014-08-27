/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 *
 * Graphical Session Manager
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

int main(int argc, char * argv[]) {
	/* Starts a graphical session and then spins waiting for a kill (logout) signal */

	int _wallpaper_pid = fork();
	if (!_wallpaper_pid) {
		char * args[] = {"/bin/wallpaper", NULL};
		execvp(args[0], args);
	}
	int _panel_pid = fork();
	if (!_panel_pid) {
		char * args[] = {"/bin/panel", NULL};
		execvp(args[0], args);
	}
	int _toastd_pid = fork();
	if (!_toastd_pid) {
		char * args[] = {"/bin/toastd", NULL};
		execvp(args[0], args);
	}

	wait(NULL);

	int pid;
	do {
		pid = waitpid(-1, NULL, 0);
	} while ((pid > 0) || (pid == -1 && errno == EINTR));

	return 0;
}
