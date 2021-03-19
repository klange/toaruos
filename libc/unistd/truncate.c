#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

int truncate(const char * path, off_t length) {
	errno = EINVAL;
	return -1;
}

