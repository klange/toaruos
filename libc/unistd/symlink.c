#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <libc/syscall.h>
#include <sys/syscall.h>


DEFN_SYSCALL3(symlinkat, SYS_SYMLINKAT, const char *, int, const char *);

int symlinkat(const char *target, int fd, const char *name) {
	__sets_errno(syscall_symlinkat(target, fd, name));
}

int symlink(const char *target, const char *name) {
	return symlinkat(target, AT_FDCWD, name);
}
