#include <stdlib.h>
#include <string.h>

int setenv(const char *name, const char *value, int overwrite) {
	if (!overwrite) {
		char * tmp = getenv(name);
		if (tmp)
			return 0;
	}
	char * tmp = malloc(strlen(name) + strlen(value) + 2);
	*tmp = '\0';
	strcat(tmp, name);
	strcat(tmp, "=");
	strcat(tmp, value);
	return putenv(tmp);
}
