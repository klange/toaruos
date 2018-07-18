#include <syscall.h>
#include <errno.h>

int mount(char * source, char * target, char * type, unsigned long flags, void * data) {
	__sets_errno(syscall_mount(source, target, type, flags, data));
}

