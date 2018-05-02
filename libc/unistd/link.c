#include <unistd.h>
#include <errno.h>

// TODO:
//  We have a system call for this?
int link(const char *old, const char *new) {
	errno = EMLINK;
	return -1;
}
