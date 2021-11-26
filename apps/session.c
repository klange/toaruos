/**
 * @brief session - UI session manager
 *
 * Runs the user's yutanirc or starts up a panel and desktop
 * if they don't have one. Generally run by glogin.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

int main(int argc, char * argv[]) {

	char path[1024];
	char * home = getenv("HOME");
	if (home) {
		sprintf(path, "%s/.yutanirc", home);
		char * args[] = {path, NULL};
		execvp(args[0], args);
	}

	/* Fallback */

	int _background_pid = fork();
	if (!_background_pid) {
		sprintf(path, "%s/Desktop", home);
		chdir(path);
		char * args[] = {"/bin/file-browser", "--wallpaper", NULL};
		execvp(args[0], args);
	}

	int _panel_pid = fork();
	if (!_panel_pid) {
		char * args[] = {"/bin/panel", "--really", NULL};
		execvp(args[0], args);
	}

	wait(NULL);

	int pid;
	do {
		pid = waitpid(-1, NULL, 0);
	} while ((pid > 0) || (pid == -1 && errno == EINTR));

	return 0;
}
