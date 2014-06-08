/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2014 Kevin Lange
 */
#include <stdlib.h>
#include <stdint.h>
#include <syscall.h>
#include <signal.h>
#include "pthread.h"

#define PTHREAD_STACK_SIZE 0x100000

int clone(uintptr_t a,uintptr_t b,void* c) {
	return syscall_clone(a,b,c);
}
int gettid() {
	return syscall_gettid();
}

int pthread_create(pthread_t * thread, pthread_attr_t * attr, void *(*start_routine)(void *), void * arg) {
	char * stack = malloc(PTHREAD_STACK_SIZE);
	uintptr_t stack_top = (uintptr_t)stack + PTHREAD_STACK_SIZE;
	thread->stack = stack;
	thread->id = clone(stack_top, (uintptr_t)start_routine, arg);
	return 0;
}

int pthread_kill(pthread_t thread, int sig) {
	return kill(thread.id, sig);
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
