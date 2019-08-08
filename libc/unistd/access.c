#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(access, SYS_ACCESS, char *, int);

int access(const char *pathname, int mode) {
	int result = syscall_access((char *)pathname, mode);
	if (result < 0) {
		errno = ENOENT; /* XXX */
		return -1;
	}
	return result;
}

