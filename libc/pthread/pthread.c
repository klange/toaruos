/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2018 K. Lange
 */
#include <stdlib.h>
#include <stdint.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

DEFN_SYSCALL3(clone, SYS_CLONE, uintptr_t, uintptr_t, void *);
DEFN_SYSCALL0(gettid, SYS_GETTID);

#define PTHREAD_STACK_SIZE 0x100000

int clone(uintptr_t a,uintptr_t b,void* c) {
	__sets_errno(syscall_clone(a,b,c));
}
int gettid() {
	return syscall_gettid(); /* never fails */
}

int pthread_create(pthread_t * thread, pthread_attr_t * attr, void *(*start_routine)(void *), void * arg) {
	char * stack = malloc(PTHREAD_STACK_SIZE);
	uintptr_t stack_top = (uintptr_t)stack + PTHREAD_STACK_SIZE;
	thread->stack = stack;
	thread->id = clone(stack_top, (uintptr_t)start_routine, arg);
	return 0;
}

int pthread_kill(pthread_t thread, int sig) {
	__sets_errno(kill(thread.id, sig));
}

void pthread_exit(void * value) {
	/* Perform nice cleanup */
#if 0
	/* XXX: LOCK */
	free(stack);
	/* XXX: Return value!? */
#endif
	uintptr_t magic_exit_target = 0xFFFFB00F;
	void (*magic_exit_func)(void) = (void *)magic_exit_target;
	magic_exit_func();
}
