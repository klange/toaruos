#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>

static int __mktemp(char * template, int (*callback)(char * path)) {
	/* Duplicating this code from mktemp so we can just remove mktemp if we feel like it. */
	size_t template_len = strlen(template);

	if (template_len < 6 || strcmp(template + template_len - 6, "XXXXXX")) {
		/* We don't have to modify 'template' on error. */
		errno = EINVAL;
		return -1;
	}

	struct timeval now;
	char tmp[7] = {0};

	for (int attempts = 0; attempts < 1000; ++attempts) {
		gettimeofday(&now, NULL);
		snprintf(tmp, 7, "%06ld", now.tv_usec);
		memcpy(template + template_len - 6, tmp, 6);
		int ret;
		if ((ret = callback(template)) != -1) return ret;
		if (errno != EEXIST) return -1;
	}

	/* Ran out of attempts */
	return -1;
}

static int __mktemp_int(char * path) {
	int ret = access(path, F_OK);
	if (ret == -1 && errno == ENOENT) return 0;
	if (ret == 0) errno = EEXIST;
	return -1;
}

/* mktemp was removed in POSIX.1-2008, but it's still expected as part of C. */
char * mktemp(char * template) {
	if (!__mktemp(template, __mktemp_int)) return template;
	*template = 0;
	return template;
}

static int __mkstemp_int(char * path) {
	return open(path, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
}

int mkstemp(char * template) {
	return __mktemp(template, __mkstemp_int);
}

static int __mkdtemp_int(char * path) {
	return mkdir(path, S_IRWXU);
}

char * mkdtemp(char * template) {
	if (!__mktemp(template, __mkdtemp_int)) return template;
	return NULL;
}
