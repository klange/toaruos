#include <unistd.h>
#include <syscall.h>

DEFN_SYSCALL0(getuid, 23);

int getuid() {
	return syscall_getuid();
}
