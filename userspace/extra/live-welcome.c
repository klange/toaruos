#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "lib/toaru_auth.h"

#include "lib/trace.h"
#define TRACE_APP_NAME "live-welcome"

int main(int argc, char * argv[]) {
	TRACE("Starting session manager and launching demo...");

	int _session_pid = fork();
	if (!_session_pid) {
		setuid(1000);
		toaru_auth_set_vars();

		char * args[] = {"/bin/gsession", NULL};
		execvp(args[0], args);
		TRACE("gsession start failed?");
	}

	int _wizard_pid = fork();
	if (!_wizard_pid) {
		setuid(1000);
		toaru_auth_set_vars();

		char * args[] = {"/bin/live-wizard", NULL};
		execvp(args[0], args);
		TRACE("wizard start failed?");
	}

	int pid = 0;
	do {
		pid = wait(NULL);
	} while ((pid > 0 && pid != _session_pid) || (pid == -1 && errno == EINTR));

	system("reboot");

}
