#include <unistd.h>
#include <syscall.h>

DEFN_SYSCALL3(lseek, 14, int, int, int);

off_t lseek(int file, off_t ptr, int dir) {
	return syscall_lseek(file,ptr,dir);
}

