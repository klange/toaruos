/*
 * shmtest
 * It's an shmtest.
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>


int main (int argc, char ** argv) {
	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}
	char * tokens[] = { NULL, argv[1], NULL };

	int pid = getpid();
	uint32_t f = fork();
	if (getpid() != pid) {
		// Child: client
		tokens[0] = "/bin/shm_client";
		execvp(tokens[0], tokens);
		return 3;
	} else {
		// Parent: server
		tokens[0] = "/bin/shm_server";
		execvp(tokens[0], tokens);
		return 4;
	}

	return 0;
}
