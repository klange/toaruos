#include <syscall.h>
#include <syscall_nums.h>
#include <sched.h>
#include <errno.h>

DEFN_SYSCALL0(yield, SYS_YIELD);

int sched_yield(void) {
	__sets_errno(syscall_yield());
}
