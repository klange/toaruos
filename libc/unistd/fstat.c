#include <unistd.h>
#include <sys/stat.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(fstat, SYS_STAT, int, void *);

int fstat(int file, struct stat *st) {
	__sets_errno(syscall_fstat(file, st));
}
