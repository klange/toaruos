#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

DEFN_SYSCALL2(shm_obtain, 35, char *, int)
DEFN_SYSCALL1(shm_release, 36, char *)


int main (int argc, char ** argv) {
	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	printf("(this should not crash; but the kernel should free the shm block)\n");

	volatile char * shm = (char *)syscall_shm_obtain(argv[1], 0x1000);
	if (shm == NULL) {
		return 1;
	}

	char * args[] = {"/bin/echo", "exec'd to echo\n", NULL};
	execve(args[0], args, NULL);

	return 5;
}
