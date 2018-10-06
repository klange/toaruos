#include <stdio.h>
#include <errno.h>

void perror(const char *s) {
	fprintf(stderr, "%s: %s\n", s, strerror(errno));
}
