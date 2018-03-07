#include <stdio.h>
#include <string.h>

int puts(const char *s) {
	size_t l = strlen(s);
	if (fwrite(s, 1, strlen(s), stdout) < 0) {
		return EOF;
	}
	if (fwrite("\n", 1, 1, stdout) < 0) {
		return EOF;
	}
	return 0;
}
