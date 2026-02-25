#include <stdlib.h>
#include <string.h>
#include <errno.h>

int setenv(const char *name, const char *value, int overwrite) {
	if (!overwrite && getenv(name)) return 0;

	if (!name || !*name) {
		errno = EINVAL;
		return -1;
	}

	char * c = strchrnul(name, '=');

	if (*c) {
		errno = EINVAL;
		return -1;
	}

	char * tmp = malloc(strlen(name) + strlen(value) + 2);
	*tmp = '\0';
	strcat(tmp, name);
	strcat(tmp, "=");
	strcat(tmp, value);
	return putenv(tmp);
}
