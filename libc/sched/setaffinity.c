#define _TOARU_SOURCE
#include <sched.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL2(set_affinity, SYS_SET_AFFINITY, int, uint64_t);

int sched_setaffinity_t(pid_t p, uint64_t cpuset) {
	__sets_errno(syscall_set_affinity(p, cpuset));
}

DEFN_SYSCALL2(get_affinity, SYS_GET_AFFINITY, int, uint64_t*);

int sched_getaffinity_t(pid_t p, uint64_t *cpuset) {
	__sets_errno(syscall_get_affinity(p, cpuset));
}
