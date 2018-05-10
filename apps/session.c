#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

int main(int argc, char * argv[]) {
	int _background_pid = fork();
	if (!_background_pid) {
		char * args[] = {"/bin/background", "--really", NULL};
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
