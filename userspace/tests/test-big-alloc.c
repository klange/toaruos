#include <stdio.h>
#include <stdlib.h>

#include <sys/wait.h>

#include "lib/pthread.h"

char * x;
int i;

void *print_pid(void * garbage) {
	printf("x[%d] = %d\n", i, x[i]);
	pthread_exit(NULL);
}

int main(int argc, char * argv[]) {
	printf("Making a big allocation!\n");
	x = malloc(0x400000);
	x[0x355555] = 'a';
	i = atoi(argv[1]);
	pthread_t thread;
	pthread_create(&thread, NULL, print_pid, NULL);

	waitpid(thread.id, NULL, 0);
	return x[i];
}
