#include <errno.h>
#include <syscall.h>
#include <sys/stat.h>

int mkdir(const char *pathname, mode_t mode) {
	int ret = syscall_mkdir((char *)pathname, mode);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}
