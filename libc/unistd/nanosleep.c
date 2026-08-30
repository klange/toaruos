#include <unistd.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <time.h>
#include <errno.h>

DEFN_SYSCALL2(nanosleep,  SYS_NANOSLEEP, const struct timespec *, struct timespec *);

int nanosleep(const struct timespec *rqtp, struct timespec *rmtp) {
	__sets_errno(syscall_nanosleep(rqtp, rmtp));
}

