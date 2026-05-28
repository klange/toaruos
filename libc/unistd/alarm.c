#include <unistd.h>
#include <stdio.h>

extern char ** __argv;

unsigned int alarm(unsigned int seconds) {
	fprintf(stderr, "%s: alarm requested (%d seconds)\n", __argv[0], seconds);
	return 0;
}
