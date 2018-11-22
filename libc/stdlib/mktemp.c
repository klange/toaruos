#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

char * mktemp(char * template) {
	if (strstr(template + strlen(template)-6, "XXXXXX") != template + strlen(template) - 6) {
		errno = EINVAL;
		return NULL;
	}
	static int _i = 0;
	char tmp[7] = {0};
	sprintf(tmp,"%04d%02d", getpid(), _i++);
	memcpy(template + strlen(template) - 6, tmp, 6);
	return template;
}

int mkstemp(char * template) {
	mktemp(template);
	return open(template, O_RDWR | O_CREAT, 0600);
}
