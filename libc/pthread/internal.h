#pragma once

#include <sys/types.h>

#define PTHREAD_STACK_SIZE 0x100000

struct __pthread {
	pid_t tid;
	void * (*entry)(void*);
	void * arg;
	int * err_addr;
	int   thread_err_val;
};

void * __tls_get_addr(void*);
void __make_tls(void);

extern int __errno __asm__("errno");
