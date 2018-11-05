#include <stdlib.h>
#include <unistd.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/wait.h>

int system(const char * command) {
	char * args[] = {
		"/bin/sh",
		"-c",
		(char *)command,
		NULL,
	};
	pid_t pid = fork();
	if (!pid) {
		execvp(args[0], args);
		exit(1);
	} else {
		int status;
		waitpid(pid, &status, 0);
		return WEXITSTATUS(status);
	}
}
