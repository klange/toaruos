#include <syscall.h>
#include <errno.h>

int mount(char * source, char * target, char * type, unsigned long flags, void * data) {
	int r = syscall_mount(source, target, type, flags, data);

	if (r < 0) {
		errno = -r;
		return -1;
	}

	return r;
}

