#include <stdlib.h>
#include <syscall.h>

void abort(void) {
	syscall_exit(-1);
	__builtin_unreachable();
}
