#include <unistd.h>
#include <errno.h>

int access(const char *pathname, int mode) {
	int result = syscall_access((char *)pathname, mode);
	if (result < 0) {
		errno = ENOENT; /* XXX */
	}
	return result;
}

