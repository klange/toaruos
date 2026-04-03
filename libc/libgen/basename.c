#include <libgen.h>
#include <stdlib.h>
#include <string.h>

char * basename(char * path) {
	char * s = path;
	char * c = NULL;
	do {
		char * maybe_slash = s;
		while (*s == '/') s++;
		if (!*s) {
			while (maybe_slash != s) *maybe_slash++ = '\0';
			goto _done;
		}
		c = s;
		s = strchr(c,'/');
	} while (s);

_done:

	if (!c) {
		return "/";
	}

	return c;
}
