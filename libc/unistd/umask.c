#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL1(umask, SYS_UMASK, int);

mode_t umask(mode_t mask) {
	__sets_errno(syscall_umask(mask));
}
