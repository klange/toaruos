#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(shm_obtain,  SYS_SHM_OBTAIN, char *, size_t *);
DEFN_SYSCALL1(shm_release, SYS_SHM_RELEASE, char *);
