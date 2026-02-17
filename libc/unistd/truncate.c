#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(truncate, SYS_TRUNCATE, char *, off_t);

int truncate(const char * path, off_t length) {
	__sets_errno(syscall_truncate((char*)path, length));
}

DEFN_SYSCALL2(ftruncate, SYS_FTRUNCATE, int, off_t);

int ftruncate(int fd, off_t length) {
	__sets_errno(syscall_ftruncate(fd, length));
}

