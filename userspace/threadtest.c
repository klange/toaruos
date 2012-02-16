#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <stdlib.h>
#include <stdint.h>

DEFN_SYSCALL3(clone, 30, uintptr_t, uintptr_t, void *);
DEFN_SYSCALL0(gettid, 41);

int clone(uintptr_t,uintptr_t,void*) __attribute__((alias("syscall_clone")));
int gettid() __attribute__((alias("syscall_gettid")));

typedef struct {
	uint32_t id;
	char * stack;
	void * ret_val;
} pthread_t;
typedef unsigned int pthread_attr_t;

#define PTHREAD_STACK_SIZE 10240

int pthread_create(pthread_t * thread, pthread_attr_t * attr, void *(*start_routine)(void *), void * arg) {
	char * stack = malloc(PTHREAD_STACK_SIZE);
	uintptr_t stack_top = (uintptr_t)stack + PTHREAD_STACK_SIZE;
	thread->stack = stack;
	thread->id = clone(stack_top, (uintptr_t)start_routine, arg);
	return 0;
}

void pthread_exit(void * value) {
	/* Perform nice cleanup */
#if 0
	/* XXX: LOCK */
	free(stack);
	/* XXX: Return value!? */
#endif
	__asm__ ("jmp 0xFFFFB00F"); /* Force thread exit */
}

#define NUM_THREADS 5
#define VALUE      0x1000000
#define CHECKPOINT 0x03FFFFF

volatile uint32_t result = 0;

void *print_pid(void * garbage) {
	printf("I am a thread and my pid is %d but my tid is %d\n", getpid(), gettid());

	for (uint32_t i = 0; i < VALUE; ++i) {
		if (!(result & CHECKPOINT)) {
			printf("[%d] Checkpoint: %x\n", gettid(), result);
		}
		result++;
	}

	pthread_exit(garbage);
}

int main(int argc, char * argv[]) {
	pthread_t thread[NUM_THREADS];
	printf("I am the main process and my pid is %d and my tid is also %d\n", getpid(), gettid());

	printf("Attempting to unsafely calculate %d!\n", NUM_THREADS * VALUE);

	for (int i = 0; i < NUM_THREADS; ++i) {
		pthread_create(&thread[i], NULL, print_pid, NULL);
	}

	for (int i = 0; i < NUM_THREADS; ++i) {
		syscall_wait(thread[i].id);
	}

	printf("Done. Result of (potentially unsafe) computation was %d %s!!\n", result,
			(result == NUM_THREADS * VALUE) ? "(yay, that's right!)" : "(boo, that's wrong!)");

	return 0;
}
