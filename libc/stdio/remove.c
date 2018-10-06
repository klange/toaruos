#include <stdio.h>
#include <unistd.h>

int remove(const char * pathname) {
	/* TODO directories */
	return unlink(pathname);
}
