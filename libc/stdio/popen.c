#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "stdio_internal.h"

FILE * popen(const char * command, const char * type) {
	if (!type || (*type != 'r' && *type != 'w') || type[1]) {
		errno = EINVAL;
		return NULL;
	}

	int pipe_fd[2];
	if (pipe(pipe_fd) < 0) return NULL;

	FILE * out = fdopen(pipe_fd[*type=='w'], type);
	if (!out) {
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		return NULL;
	}

	pid_t child = fork();
	if (child < 0) {
		fclose(out);
		return NULL;
	}

	if (!child) {
		dup2(pipe_fd[*type=='r'], *type=='r');
		close(pipe_fd[*type=='w']);

		char * args[] = {
			"/bin/sh",
			"-c",
			(char *)command,
			NULL,
		};

		execvp(args[0], args);
		abort();
		__builtin_unreachable();
	}

	close(pipe_fd[*type=='r']);
	out->popen_pid = child;

	return out;
}

int pclose(FILE * stream) {
	pid_t me = stream->popen_pid;

	/* Close the stream. */
	fclose(stream);

	/* Wait for the process */
	int ret = 0;
	int status;
	while ((ret = waitpid(me, &status, 0)) == EINTR);
	if (ret < 0) return -1;
	return status;
}
