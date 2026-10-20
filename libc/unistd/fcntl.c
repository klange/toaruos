#include <errno.h>
#include <fcntl.h>
#include <va_list.h>
#include <stdint.h>
#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL3(fcntl, SYS_FCNTL, int, int, long);

int fcntl(int fd, int cmd, ...) {
	va_list ap;
	va_start(ap, cmd);
	unsigned long arg = va_arg(ap, unsigned long);
	va_end(ap);
	__sets_errno(syscall_fcntl(fd, cmd, arg));
}
