#include <unistd.h>
#include <sys/stat.h>
#include <syscall.h>

DEFN_SYSCALL2(fstat, 15, int, void *);

int fstat(int file, struct stat *st) {
	syscall_fstat(file, st);
	return 0;
}
