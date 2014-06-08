/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
/*
 * threadtest
 *
 * A class concurreny failure demonstration.
 * Append -l to use locks.
 */
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include "lib/pthread.h"
#include "lib/spinlock.h"

#define NUM_THREADS 5
#define VALUE      0x1000000
#define CHECKPOINT 0x03FFFFF

volatile uint32_t result = 0;
int8_t use_locks = 0;

volatile int the_lock = 0;

void *print_pid(void * garbage) {
	int i;
	printf("I am a thread and my pid is %d but my tid is %d and my stack is at %p\n", getpid(), gettid(), &i);

	for (uint32_t i = 0; i < VALUE; ++i) {
		if (use_locks) {
			spin_lock(&the_lock);
		}
		if (!(result & CHECKPOINT)) {
			printf("[%d] Checkpoint: %x\n", gettid(), result);
		}
		result++;
		if (use_locks) {
			spin_unlock(&the_lock);
		}
	}

	pthread_exit(garbage);
}

int main(int argc, char * argv[]) {
	if (argc > 1) {
		if (!strcmp(argv[1], "-l")) {
			use_locks = 1;
		}
	}
	pthread_t thread[NUM_THREADS];
	printf("I am the main process and my pid is %d and my tid is also %d\n", getpid(), gettid());

	printf("Attempting to %s calculate %d!\n",
			(use_locks) ? "(safely)" : "(unsafely)",
			NUM_THREADS * VALUE);

	for (int i = 0; i < NUM_THREADS; ++i) {
		pthread_create(&thread[i], NULL, print_pid, NULL);
	}

	for (int i = 0; i < NUM_THREADS; ++i) {
		waitpid(thread[i].id, NULL, 0);
	}

	printf("Done. Result of %scomputation was %d %s!!\n",
			(use_locks) ? "" : "(definitely unsafe) ",
			result,
			(result == NUM_THREADS * VALUE) ? "(yay, that's right!)" : "(boo, that's wrong!)");

	return 0;
}
