#include <sys/time.h>
#include <errno.h>

int futimes(int fd, const struct timeval times[2]) {
	errno = ENOTSUP;
	return -1;
}

int utimes(const char *path, const struct timeval times[2]) {
	errno = ENOTSUP;
	return -1;
}
