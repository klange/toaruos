#include <sys/times.h>
#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL1(times, SYS_TIMES, struct tms *);

clock_t times(struct tms * buf) {
	__sets_errno(syscall_times(buf));
}

