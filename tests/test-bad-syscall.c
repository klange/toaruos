#include <stdio.h>
#include <sys/syscall.h>

#include "../libc/syscall.h"

DEFN_SYSCALL0(getpid, SYS_GETPID);

int main(int argc, char * argv[]) {
	fprintf(stderr, "Can I make system calls?\n");
	int pid = syscall_getpid();
	fprintf(stderr, "getpid=%d\n", pid);
	return 0;
}
