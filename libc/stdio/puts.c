#include <stdio.h>
#include <string.h>

int puts(const char *s) {
	/* eof? */
	fwrite(s, 1, strlen(s), stdout);
	fwrite("\n", 1, 1, stdout);
	return 0;
}
