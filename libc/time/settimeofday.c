#include <sys/time.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(settimeofday, SYS_SETTIMEOFDAY, void *, void *);

int settimeofday(struct timeval *p, void *z){
	return syscall_settimeofday(p,z);
}


