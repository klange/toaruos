#include <unistd.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL3(lseek, SYS_SEEK, int, int, int);

off_t lseek(int file, off_t ptr, int dir) {
	__sets_errno(syscall_lseek(file,ptr,dir));
}

