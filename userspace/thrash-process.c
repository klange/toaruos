#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

DEFN_SYSCALL1(wait, 17, unsigned int);

int main(int argc, char ** argv) {
	for (int j = 0; j < 1024; ++j) {
		volatile int k = fork();
		printf("I am %d, I got %d\n", getpid(), k);
		if (k == 0) {
			printf("I am %d\n", getpid());
			return 0;
		} else {
			printf("Waiting on %d\n", k);
			syscall_wait(k);
		}
	}
	return 0;
}
