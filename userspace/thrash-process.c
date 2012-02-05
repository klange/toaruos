#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char ** argv) {
	int quiet = 0;
	if (argc > 1) {
		if (!strcmp(argv[1],"-q")) {
			printf("I'll be quiet...\n");
			quiet = 1;
		}
	}
	for (int j = 0; j < 1024; ++j) {
		volatile int k = fork();
		if (!quiet)
			printf("I am %d, I got %d\n", getpid(), k);
		if (k == 0) {
			if (!quiet || !(j % 10))
				printf("I am %d\n", getpid());
			return 0;
		} else {
			if (!quiet)
				printf("Waiting on %d\n", k);
			syscall_wait(k);
		}
	}
	return 0;
}
