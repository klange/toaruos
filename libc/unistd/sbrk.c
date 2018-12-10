#include <unistd.h>
#include <syscall_nums.h>
#include <syscall.h>

DEFN_SYSCALL1(sbrk,  SYS_SBRK, int);

void *sbrk(intptr_t increment) {
	return (void *)syscall_sbrk(increment);
}
