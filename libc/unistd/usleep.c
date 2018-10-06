#include <unistd.h>
#include <syscall.h>

DEFN_SYSCALL2(nanosleep,  46, unsigned long, unsigned long);

int usleep(useconds_t usec) {
	syscall_nanosleep(0, usec / 10000);
	return 0;
}

