#include <syscall.h>
#include <errno.h>

unsigned int sleep(unsigned int seconds) {
	__sets_errno(syscall_sleep(seconds, 0));
}

