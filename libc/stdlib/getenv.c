#include <string.h>
#include <stdlib.h>

extern char ** environ;

char * getenv(const char *name) {
	char ** e = environ;
	size_t len = strlen(name);
	while (*e) {
		char * t = *e;
		if (strstr(t, name) == *e) {
			if (t[len] == '=') {
				return &t[len+1];
			}
		}
		e++;
	}
	return NULL;
}
