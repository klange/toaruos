#pragma once

#define PTHREAD_STACK_SIZE 0x100000

struct __pthread {
	pid_t tid;
	void * (*entry)(void*);
	void * arg;
};

void * __tls_get_addr(void*);
void __make_tls(void);
