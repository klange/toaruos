#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern char ** environ;
extern int _environ_size;

static int why_no_strnstr(char * a, char * b, int n) {
	for (int i = 0; (i < n) && (a[i]) && (b[i]); ++i) {
		if (a[i] != b[i]) {
			return 1;
		}
	}
	return 0;
}

int unsetenv(const char * str) {
	int last_index = -1;
	int found_index = -1;
	int len = strlen(str);

	for (int i = 0; environ[i]; ++i) {
		if (found_index == -1 && (strstr(environ[i], str) == environ[i] && environ[i][len] == '=')) {
			found_index = i;
		}
		last_index = i;
	}

	if (found_index == -1) {
		/* not found = success */
		return 0;
	}

	if (last_index == found_index) {
		/* Was last element */
		environ[last_index] = NULL;
		return 0;
	}

	/* Was not last element, swap ordering */
	environ[found_index] = environ[last_index];
	environ[last_index] = NULL;
	return 0;
}


int putenv(char * string) {
	char name[strlen(string)+1];
	strcpy(name, string);
	char * c = strchr(name, '=');
	if (!c) {
		return 1;
	}
	*c = NULL;

	int s = strlen(name);

	int i;
	for (i = 0; i < (_environ_size - 1) && environ[i]; ++i) {
		if (!why_no_strnstr(name, environ[i], s) && environ[i][s] == '=') {
			environ[i] = string;
			return 0;
		}
	}
	/* Not found */
	if (i == _environ_size - 1) {
		int _new_environ_size = _environ_size * 2;
		char ** new_environ = malloc(sizeof(char*) * _new_environ_size);
		int j = 0;
		while (j < _new_environ_size && environ[j]) {
			new_environ[j] = environ[j];
			j++;
		}
		while (j < _new_environ_size) {
			new_environ[j] = NULL;
			j++;
		}
		_environ_size = _new_environ_size;
		environ = new_environ;
	}

	environ[i] = string;
	return 0;
}
