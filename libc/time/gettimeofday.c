#include <sys/time.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL2(gettimeofday, SYS_GETTIMEOFDAY, void *, void *);

int gettimeofday(struct timeval *p, void *z){
	__sets_errno(syscall_gettimeofday(p,z));
}

