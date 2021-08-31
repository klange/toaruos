#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(getgroups, SYS_GETGROUPS, int, gid_t *);

int getgroups(int size, gid_t list[]) {
	__sets_errno(syscall_getgroups(size, list));
}

