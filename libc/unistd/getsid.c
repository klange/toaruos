#include <unistd.h>
#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL1(getsid, SYS_GETSID, pid_t);

pid_t getsid(pid_t pid) {
	__sets_errno(syscall_getsid(pid));
}
