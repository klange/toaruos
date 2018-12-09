#include <syscall.h>
#include <syscall_nums.h>

unsigned int sleep(unsigned int seconds) {
	syscall_nanosleep(seconds, 0);
	return 0;
}

