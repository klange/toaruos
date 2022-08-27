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
#include <string.h>
#include <stdio.h>

#include <sys/wait.h>
#include <sys/sysfunc.h>

DEFN_SYSCALL3(clone, SYS_CLONE, uintptr_t, uintptr_t, void *);
DEFN_SYSCALL0(gettid, SYS_GETTID);

struct __pthread {
	pid_t tid;
	void * (*entry)(void*);
	void * arg;
};

extern int __libc_is_multicore;
static inline void _yield(void) {
	if (!__libc_is_multicore) syscall_yield();
}

#define PTHREAD_STACK_SIZE 0x100000

int clone(uintptr_t a,uintptr_t b,void* c) {
	__sets_errno(syscall_clone(a,b,c));
}
int gettid() {
	return syscall_gettid(); /* never fails */
}

void * __tls_get_addr(void* input) {
#ifdef __x86_64__
	struct tls_index {
		uintptr_t module;
		uintptr_t offset;
	};
	struct tls_index * index = input;
	/* We only support initial-exec stuff, so this must be %fs:offset */
	uintptr_t threadbase;
	asm ("mov %%fs:0, %0" :"=r"(threadbase));
	return (void*)(threadbase + index->offset);
#else
	return NULL;
#endif
}

void __make_tls(void) {
	char * tlsSpace = valloc(4096);
	memset(tlsSpace, 0x0, 4096);
	/* self-pointer start? */
	char ** tlsSelf = (char **)(tlsSpace);
	*tlsSelf = (char*)tlsSelf;
	sysfunc(TOARU_SYS_FUNC_SETGSBASE, (char*[]){(char*)tlsSelf});
}

void pthread_exit(void * value) {
	syscall_exit(0);
	__builtin_unreachable();
}

void * __thread_start(void * pthreadbase) {
	struct __pthread * this = pthreadbase;
	this->tid = gettid();
	char ** tlsbase = (char**)((char*)this + 4096);
	*tlsbase = (char*)tlsbase;
	sysfunc(TOARU_SYS_FUNC_SETGSBASE, (char*[]){(char*)tlsbase});
	pthread_exit(this->entry(this->arg));
	return NULL;
}

int pthread_create(pthread_t * thread, pthread_attr_t * attr, void *(*start_routine)(void *), void * arg) {
	char * stack = valloc(PTHREAD_STACK_SIZE + 8192);
	memset(stack, 0, PTHREAD_STACK_SIZE + 8192);
	struct __pthread * this = (void*)(stack + PTHREAD_STACK_SIZE);
	*thread = this;
	this->entry = start_routine;
	this->arg = arg;
	clone((uintptr_t)this, (uintptr_t)__thread_start, this);
	return 0;
}

int pthread_kill(pthread_t thread, int sig) {
	__sets_errno(kill(thread->tid, sig));
}

void pthread_cleanup_push(void (*routine)(void *), void *arg) {
	/* do nothing */
}

void pthread_cleanup_pop(int execute) {
	/* do nothing */
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
	while (__sync_lock_test_and_set(mutex, 0x01)) {
		_yield();
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
	int result = waitpid(thread->tid, &status, 0);
	if (retval) {
		*retval = (void*)(uintptr_t)status;
	}
	return result;
}
