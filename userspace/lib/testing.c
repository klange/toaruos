#include <stdlib.h>
#include <stdio.h>

#include "testing.h"

void notice(char * type, char * fmt, ...) {
	va_list argp;
	va_start(argp, fmt);
	/* core-tests header */

	printf("core-tests : %s : ", type);
	vprintf(fmt, argp);
	printf("\n");
}
