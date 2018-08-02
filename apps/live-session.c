#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <toaru/auth.h>
#include <toaru/yutani.h>
#include <toaru/trace.h>
#define TRACE_APP_NAME "live-session"

int main(int argc, char * argv[]) {
	int pid;

	if (getuid() != 0) {
		return 1;
	}

	int _session_pid = fork();
	if (!_session_pid) {
		setuid(1000);
		toaru_auth_set_vars();

		char * args[] = {"/bin/session", NULL};
		execvp(args[0], args);

		return 1;
	}

	/* Dummy session for live-session prevents compositor from killing itself
	 * when the main session dies the first time. */
	yutani_init();

	do {
		pid = wait(NULL);
	} while ((pid > 0 && pid != _session_pid) || (pid == -1 && errno == EINTR));

	TRACE("Live session has ended, launching graphical login.");
	int _glogin_pid = fork();
	if (!_glogin_pid) {
		char * args[] = {"/bin/glogin",NULL};
		execvp(args[0],args);
		system("reboot");
	}

	do {
		pid = wait(NULL);
	} while ((pid > 0 && pid != _glogin_pid) || (pid == -1 && errno == EINTR));

	return 0;
}
