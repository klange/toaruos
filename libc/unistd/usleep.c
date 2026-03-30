#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(sleep,  SYS_SLEEP, unsigned long, unsigned long);

static int usleep_wrap(useconds_t usec) {
	__sets_errno(syscall_sleep((usec / 10000) / 1000, (usec / 10000) % 1000));
}

int usleep(useconds_t usec) {
	return (usleep_wrap(usec) < 0) ? -1 : 0;
}

