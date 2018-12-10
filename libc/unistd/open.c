#include <unistd.h>
#include <fcntl.h>
#include <va_list.h>
#include <errno.h>

#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(open,  SYS_OPEN, const char *, int, int);

int open(const char *name, int flags, ...) {
	va_list argp;
	int mode = 0;
	int result;
	va_start(argp, flags);
	if (flags & O_CREAT) mode = va_arg(argp, int);
	va_end(argp);

	result = syscall_open(name, flags, mode);
	if (result == -1) {
		/* Not sure this is necessary */
		if (flags & O_CREAT) {
			errno = EACCES;
		} else {
			errno = ENOENT;
		}
	} else if (result < 0) {
		errno = -result;
		result = -1;
	}
	return result;
}

