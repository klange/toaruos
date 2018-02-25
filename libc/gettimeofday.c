#include <sys/time.h>
#include <syscall.h>

int gettimeofday(struct timeval *p, void *z){
	return syscall_gettimeofday(p,z);
}

