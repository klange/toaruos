#define _TOARU_SOURCE
#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL0(gettid, SYS_GETTID);

int gettid(void) {
	return syscall_gettid(); /* never fails */
}
