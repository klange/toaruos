#include <libgen.h>
#include <stdlib.h>
#include <string.h>

char * dirname(char * path) {
	int has_slash = 0;
	char * c = path;
	while (*c) {
		if (*c == '/') {
			has_slash = 1;
		}
		c++;
	}
	if (!has_slash) {
		return ".";
	}

	c--;
	while (*c == '/') {
		*c = '\0';
		if (c == path) break;
		c--;
	}

	if (c == path) {
		return "/";
	}

	/* All trailing slashes are cleared out */
	while (*c != '/') {
		*c = '\0';
		if (c == path) break;
		c--;
	}

	if (c == path) {
		if (*c == '/') return "/";
		return ".";
	}

	while (*c == '/') {
		if (c == path) return "/";
		*c = '\0';
		c--;
	}

	return path;
}
