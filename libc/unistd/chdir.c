#include <unistd.h>
#include <syscall.h>
#include <errno.h>

DEFN_SYSCALL1(chdir, 28, char *);

int chdir(const char *path) {
	__sets_errno(syscall_chdir((char*)path));
}

