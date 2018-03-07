#include <string.h>

extern char ** environ;
extern int _environ_size;

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
