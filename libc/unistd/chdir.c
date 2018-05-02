#include <unistd.h>
#include <syscall.h>

DEFN_SYSCALL1(chdir, 28, char *);

int chdir(const char *path) {
	return syscall_chdir((char*)path);
}

