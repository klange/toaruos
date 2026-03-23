/**
 * @brief init - First process.
 *
 * `init` calls startup scripts and then waits for them to complete.
 * It also waits for orphaned proceses so they can be collected.
 *
 * `init` itself is statically-linked, so minimizing libc dependencies
 * is worthwhile as it reduces to the total size of init itself, which
 * remains in memory throughout the entire lifetime of the OS.
 *
 * Startup scripts for init are stored in /etc/startup.d and are run
 * in sorted alphabetical order. It is generally recommended that these
 * startup scripts be named with numbers at the front to ensure easy
 * ordering. This system of running a set of scripts on startup is
 * somewhat similar to how sysvinit worked, but no claims of
 * compatibility are made.
 *
 * Startup scripts can be any executable binary. Shell scripts are
 * generally used to allow easy editing, but you could also use
 * a binary (even a dynamically linked one) as a startup script.
 * `init` will wait for each startup script (that is, it will wait for
 * the original process it started to exit) before running the next one.
 * So if you wish to run daemons, be sure to fork them off and then
 * exit so that the rest of the startup process can continue.
 *
 * When the last startup script finishes, `init` will reboot the system.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include <sys/wait.h>

#define INITD_PATH "/etc/startup.d"

/* Initialize fd 0, 1, 2 */
void set_console(void) {
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_WRONLY);
	open("/dev/null", O_WRONLY);
}

/* Run a startup script and wait for it to finish */
int start_options(char * args[]) {

	/* Fork child to run script */
	int cpid = fork();

	/* Child process... */
	if (!cpid) {
		/* Pass environment from init to child */
		execve(args[0], args, environ);
		/* exec failed, exit this subprocess */
		exit(0);
	}

	/* Wait for the child process to finish */
	int pid = 0;
	do {
		/*
		 * Wait, ignoring kernel threads
		 * (which also end up as children to init)
		 */
		pid = waitpid(-1, NULL, WNOKERN);

		if (pid == -1 && errno == ECHILD) {
			/* There are no more children */
			break;
		}

		if (pid == cpid) {
			/* The child process finished */
			break;
		}

		/* Continue while no error (or error was "interrupted") */
	} while ((pid > 0) || (pid == -1 && errno == EINTR));

	return cpid;
}

static int not_hidden(const struct dirent *ent) {
	return ent->d_name[0] != '.';
}

int main(int argc, char * argv[]) {
	/* Initialize stdin/out/err */
	set_console();

	/* Get directory listing for /etc/startup.d */
	struct dirent ** entries;
	int count = scandir(INITD_PATH, &entries, not_hidden, alphasort);

	if (count < 0) {
		start_options((char *[]){"/bin/getty",NULL});
	} else {
		/* Run scripts */
		for (int i = 0; i < count; ++i) {
			char path[300];
			sprintf(path, "/etc/startup.d/%s", entries[i]->d_name);
			start_options((char *[]){path, NULL});

			free(entries[i]);
		}
		free(entries);
	}

	/* Self-explanatory */
	syscall_reboot();
	return 0;
}
