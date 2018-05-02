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

int putenv(char * string) {
	if (_environ_size == 0) {
		/* Find actual size */
		int size = 0;

		char ** tmp = environ;
		while (*tmp) {
			size++;
			tmp++;
		}

		/* Multiply by two */
		_environ_size = size * 2;

		char ** new_environ = malloc(sizeof(char*) * _environ_size);
		int i = 0;
		while (environ[i]) {
			new_environ[i] = environ[i];
			i++;
		}

		while (i < _environ_size) {
			new_environ[i] = NULL;
			i++;
		}

		environ = new_environ;
	}

	char name[strlen(string)];
	strcpy(name, string);
	char * c = strchr(name, '=');
	if (!c) {
		return 1;
	}
	*c = NULL;

	int s = strlen(name);

	int i;
	for (i = 0; environ[i]; ++i) {
		if (!why_no_strnstr(name, environ[i], s)) {
			environ[i] = string;
			return 0;
		}
	}
	/* Not found */
	if (i >= _environ_size) {
		environ = realloc(environ, _environ_size * 2);
		for (int i = _environ_size; i < _environ_size * 2; ++i) {
			environ[i] = NULL;
		}
		_environ_size *= 2;
	}

	environ[i] = string;
	return 0;
}
