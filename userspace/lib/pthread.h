#ifndef PTHREAD_H
#define PTHREAD_H

#include <stdint.h>
#include <syscall.h>

typedef struct {
	uint32_t id;
	char * stack;
	void * ret_val;
} pthread_t;
typedef unsigned int pthread_attr_t;

int pthread_create(pthread_t * thread, pthread_attr_t * attr, void *(*start_routine)(void *), void * arg);
void pthread_exit(void * value);
int pthread_kill(pthread_t thread, int sig);

int clone(uintptr_t,uintptr_t,void*);
int gettid();

#endif
