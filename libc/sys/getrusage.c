#include <syscall.h>
#include <syscall_nums.h>
#include <sys/resource.h>
#include <errno.h>

DEFN_SYSCALL2(getrusage, SYS_GETRUSAGE, int, struct rusage*);

int getrusage(int who, struct rusage* r_usage) {
	__sets_errno(syscall_getrusage(who,r_usage));
}
