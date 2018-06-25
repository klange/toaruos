#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL1(unlink, SYS_UNLINK, char *);

int unlink(const char * pathname) {
	int result = syscall_unlink((char *)pathname);
	if (result < 0) {
		errno = -result;
		return -1;
	}

	return 0;
}
