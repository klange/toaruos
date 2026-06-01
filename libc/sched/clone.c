#define _TOARU_SOURCE
#include <sched.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL3(clone, SYS_CLONE, uintptr_t, uintptr_t, void *);

int clone(uintptr_t a,uintptr_t b,void* c) {
	__sets_errno(syscall_clone(a,b,c));
}
