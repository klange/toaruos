#include <errno.h>
#include <pthread.h>

#include <libc/pthread/internal.h>

int * __errno_addr(void) {
	return pthread_self()->err_addr;
}

int __errno __asm__("errno") = 0;
