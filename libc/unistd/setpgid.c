#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(setpgid, SYS_SETPGID, int, int);

int setpgid(pid_t pid, pid_t pgid) {
	__sets_errno(syscall_setpgid((int)pid,(int)pgid));
}

