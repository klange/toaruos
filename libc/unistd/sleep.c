#include <syscall.h>

unsigned int sleep(unsigned int seconds) {
	syscall_sleep(seconds, 0);
	return 0;
}

