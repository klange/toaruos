#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(setgroups, SYS_SETGROUPS, int, const gid_t *);

int setgroups(int size, const gid_t list[]) {
	__sets_errno(syscall_setgroups(size, list));
}


