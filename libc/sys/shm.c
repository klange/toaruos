#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(shm_obtain,  SYS_SHM_OBTAIN, const char *, size_t *);
DEFN_SYSCALL1(shm_release, SYS_SHM_RELEASE, const char *);

void * shm_obtain(const char * path, size_t * size) {
	__sets_errno_type(void*,syscall_shm_obtain(path, size));
}

int shm_release(const char * path) {
	__sets_errno(syscall_shm_release(path));
}
