#include <sys/time.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(gettimeofday, SYS_GETTIMEOFDAY, void *, void *);

int gettimeofday(struct timeval *p, void *z){
	__sets_errno(syscall_gettimeofday(p,z));
}

