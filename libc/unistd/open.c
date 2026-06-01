#include <unistd.h>
#include <fcntl.h>
#include <va_list.h>
#include <errno.h>

#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL3(open,  SYS_OPEN, const char *, long, mode_t);

int open(const char *name, int flags, ...) {
	va_list argp;
	mode_t mode = 0;
	va_start(argp, flags);
	if (flags & O_CREAT) mode = va_arg(argp, mode_t);
	va_end(argp);

	__sets_errno(syscall_open(name, flags, mode));
}

