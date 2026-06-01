#include <unistd.h>
#include <sys/syscall.h>
#include <libc/syscall.h>
#include <errno.h>

DEFN_SYSCALL1(sbrk,  SYS_SBRK, int);

void *sbrk(intptr_t increment) {
	__sets_errno_type(void*,syscall_sbrk(increment));
}
