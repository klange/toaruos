/*
 * Sleep
 */
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

DEFN_SYSCALL2(nanosleep,  46, unsigned long, unsigned long);

int main(int argc, char ** argv) {
	int ret = 0;

	char * arg = strdup(argv[1]);

	float time = atof(arg);

	unsigned int seconds = (unsigned int)time;
	unsigned int subsecs = (unsigned int)((time - (float)seconds) * 100);

	ret = syscall_nanosleep(seconds, subsecs);

	return ret;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
