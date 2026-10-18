#define _TOARU_SOURCE
#include <time.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL2(alarm, SYS_ALARM, const struct timespec*, struct timespec*);

int nanoalarm(const struct timespec * new_alarm, struct timespec * old_alarm) {
	__sets_errno(syscall_alarm(new_alarm, old_alarm));
}


