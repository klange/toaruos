#include <sys/time.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL2(settimeofday, SYS_SETTIMEOFDAY, void *, void *);

int settimeofday(struct timeval *p, void *z){
	__sets_errno(syscall_settimeofday(p,z));
}

