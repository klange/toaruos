#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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
int8_t use_locks = 0;

volatile uint8_t the_lock = 0;

void spin_lock(uint8_t volatile * lock) {
	while(__sync_lock_test_and_set(lock, 0x01)) {
		;; /* oh god */
	}
}

void spin_unlock(uint8_t volatile * lock) {
	__sync_lock_release(lock);
}

void *print_pid(void * garbage) {
	printf("I am a thread and my pid is %d but my tid is %d\n", getpid(), gettid());

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
		syscall_wait(thread[i].id);
	}

	printf("Done. Result of %scomputation was %d %s!!\n",
			(use_locks) ? "" : "(definitely unsafe) ",
			result,
			(result == NUM_THREADS * VALUE) ? "(yay, that's right!)" : "(boo, that's wrong!)");

	return 0;
}
