#define _TOARU_SOURCE
#include <unistd.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL0(gettid, SYS_GETTID);

int gettid(void) {
	return syscall_gettid(); /* never fails */
}
