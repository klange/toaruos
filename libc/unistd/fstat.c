#include <unistd.h>
#include <sys/stat.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(stat, SYS_STAT, int, void *);

int fstat(int file, struct stat *st) {
	__sets_errno(syscall_stat(file, st));
}
