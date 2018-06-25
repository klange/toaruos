#include <unistd.h>
#include <errno.h>

int rmdir(const char *pathname) {
	errno = ENOTSUP;
	return -1;
}
