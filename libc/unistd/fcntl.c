#include <errno.h>
#include <fcntl.h>
#include <va_list.h>
#include <stdint.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(fcntl, SYS_FCNTL, int, int, long);

int fcntl(int fd, int cmd, ...) {
	va_list ap;
	va_start(ap, cmd);
	long arg = 0;
	switch (cmd) {
		case F_SETFD:
		case F_SETFL:
		case F_DUPFD:
			arg = va_arg(ap, int); /* "taken as an integer of type int" */
			break;
		case F_GETLK:
		case F_SETLK:
		case F_SETLKW:
			arg = (long)(uintptr_t)va_arg(ap, struct flock *);
			break;
	}
	va_end(ap);
	__sets_errno(syscall_fcntl(fd, cmd, arg));
}
