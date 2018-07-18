#include <errno.h>
#include <syscall.h>
#include <sys/stat.h>

int mkdir(const char *pathname, mode_t mode) {
	__sets_errno(syscall_mkdir((char *)pathname, mode));
}
