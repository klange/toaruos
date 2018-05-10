#include <errno.h>
#include <syscall.h>
#include <sys/stat.h>

DEFN_SYSCALL2(chmod, 50, char *, int);

int chmod(const char *path, mode_t mode) {
	int result = syscall_chmod((char *)path, mode);
	if (result < 0) {
		errno = -result;
		result = -1;
	}
	return result;
}

