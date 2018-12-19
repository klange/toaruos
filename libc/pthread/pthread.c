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

#include <sys/wait.h>

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

void pthread_cleanup_push(void (*routine)(void *), void *arg) {
	/* do nothing */
}

void pthread_cleanup_pop(int execute) {
	/* do nothing */
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
	while (__sync_lock_test_and_set(mutex, 0x01)) {
		syscall_yield();
	}
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
	if (__sync_lock_test_and_set(mutex, 0x01)) {
		return EBUSY;
	}
	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
	__sync_lock_release(mutex);
	return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
	*mutex = 0;
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
	return 0;
}

int pthread_attr_init(pthread_attr_t *attr) {
	*attr = 0;
	return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
	return 0;
}

int pthread_join(pthread_t thread, void **retval) {
	int status;
	int result = waitpid(thread.id, &status, 0);
	if (retval) {
		*retval = (void*)status;
	}
	return result;
}
