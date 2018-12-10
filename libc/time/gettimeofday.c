#include <sys/time.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(gettimeofday, SYS_GETTIMEOFDAY, void *, void *);

int gettimeofday(struct timeval *p, void *z){
	return syscall_gettimeofday(p,z);
}

