#include <unistd.h>
#include <errno.h>

#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(symlink, SYS_SYMLINK, char *, char *);

int symlink(char * target, char * name) {
	int r = syscall_symlink(target, name);

	if (r < 0) {
		errno = -r;
		return -1;
	}

	return r;
}

