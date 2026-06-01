#include <libc/syscall.h>
#include <sys/syscall.h>
#include <sys/insmod.h>
#include <errno.h>

DEFN_SYSCALL3(insmod, SYS_INSMOD, int, int, char**);

int insmod(int fd, int argc, char **argv) {
	__sets_errno(syscall_insmod(fd, argc, argv));
}
