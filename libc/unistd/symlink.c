#include <unistd.h>
#include <errno.h>

#include <libc/syscall.h>
#include <sys/syscall.h>

DEFN_SYSCALL2(symlink, SYS_SYMLINK, const char *, const char *);

int symlink(const char *target, const char *name) {
	__sets_errno(syscall_symlink(target, name));
}

