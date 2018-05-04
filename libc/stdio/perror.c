#include <stdio.h>
#include <errno.h>

void perror(const char *s) {
	fprintf(stderr, "%s: error %d\n", s, errno);
}
