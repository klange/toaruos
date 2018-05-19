#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <toaru/auth.h>
#include <toaru/trace.h>
#define TRACE_APP_NAME "live-session"

int main(int argc, char * argv[]) {
	if (getuid() != 0) {
		return 1;
	}

	TRACE("Starting live session.");

	int _session_pid = fork();
	if (!_session_pid) {
		setuid(1000);
		toaru_auth_set_vars();

		char * args[] = {"/bin/session", NULL};
		execvp(args[0], args);

		return 1;
	}

	int pid = 0;
	do {
		pid = wait(NULL);
	} while ((pid > 0 && pid != _session_pid) || (pid == -1 && errno == EINTR));

	TRACE("Live session has ended, launching graphical login.");
	char * args[] = {"/bin/glogin",NULL};
	execvp(args[0],args);

	TRACE("failed to start glogin after log out, trying to reboot instead.");
	system("reboot");


	return 0;
}
