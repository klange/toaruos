#include <utime.h>
#include <errno.h>

int utime(const char *filename, const struct utimbuf *times) {
	/* Unimplemented */
	errno = ENOTSUP;
	return -1;
}
