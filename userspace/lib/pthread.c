#include <stdlib.h>
#include <stdint.h>
#include "pthread.h"

DEFN_SYSCALL3(clone, 30, uintptr_t, uintptr_t, void *);
DEFN_SYSCALL0(gettid, 41);

#define PTHREAD_STACK_SIZE 10240

int clone(uintptr_t,uintptr_t,void*) __attribute__((alias("syscall_clone")));
int gettid() __attribute__((alias("syscall_gettid")));


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
