#include <unistd.h>
#include <errno.h>

char * ttyname(int fd) {
	errno = ENOTSUP;
	return NULL;
}
