#include <stdio.h>

extern char * _argv_0;

unsigned int alarm(unsigned int seconds) {
	fprintf(stderr, "%s: alarm requested (%d seconds)\n", _argv_0, seconds);
	return 0;
}
