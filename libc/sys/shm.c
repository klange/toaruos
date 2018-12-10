#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(shm_obtain,  SYS_SHM_OBTAIN, char *, size_t *);
DEFN_SYSCALL1(shm_release, SYS_SHM_RELEASE, char *);

void * shm_obtain(char * path, size_t * size) {
	return (void *)syscall_shm_obtain(path, size);
}

int shm_release(char * path) {
	return syscall_shm_release(path);
}
