/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2018 K. Lange
 */
#define _TOARU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>

#include <sys/wait.h>
#include <sys/mman.h>

#include <libc/pthread/internal.h>

DEFN_SYSCALL1(set_tls_base, SYS_SETTLSBASE, uintptr_t);

extern int __libc_is_multicore;
static inline void _yield(void) {
	if (!__libc_is_multicore) syscall_yield();
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

pthread_t pthread_self(void) {
	uintptr_t threadbase;
#if defined(__x86_64__)
	asm ("mov %%fs:0, %0" :"=r"(threadbase));
#elif defined(__aarch64__)
	asm volatile ("mrs %0,TPIDR_EL0" :"=r"(threadbase));
#else
# error "Unknown arch."
#endif
	return (struct __pthread*)(void*)(threadbase - 4096);
}

_hidden void __make_tls(void) {
	char * tlsSpace = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	struct __pthread * this = (void*)tlsSpace;
	this->tid = getpid();
	this->err_addr = &__errno;
	/* self-pointer start? */
	char ** tlsSelf = (char **)(tlsSpace+4096);
	*tlsSelf = (char*)tlsSelf;
	syscall_set_tls_base((uintptr_t)tlsSelf);
}

void pthread_exit(void * value) {
	syscall_exit(0);
	__builtin_unreachable();
}

static void * __thread_start(void * pthreadbase) {
	struct __pthread * this = pthreadbase;
	this->tid = gettid();
	this->err_addr = &this->thread_err_val;
	char ** tlsbase = (char**)((char*)this + 4096);
	*tlsbase = (char*)tlsbase;
	syscall_set_tls_base((uintptr_t)tlsbase);
	pthread_exit(this->entry(this->arg));
	return NULL;
}

int pthread_create(pthread_t * thread, pthread_attr_t * attr, void *(*start_routine)(void *), void * arg) {
	char * stack = mmap(NULL, PTHREAD_STACK_SIZE + 8192, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
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

int pthread_detach(pthread_t thread) {
	/* TODO */
	return 0;
}
